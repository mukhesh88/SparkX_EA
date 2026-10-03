"""
MT5 Execution Gateway & Risk Routing Module
Implements deterministic probability gating, SMC invalidation stop-losses,
dynamic position sizing, and MetaTrader 5 order dispatch.
"""

import os
import time
import logging
from dataclasses import dataclass
from typing import Optional, Dict, Any, Tuple, List

from config.trading_config import RiskConfig
from src.smc_engine import SMCFeatures
from src.laya_engine import LayaDecision

# Configure logger
logger = logging.getLogger("SparkX.ExecutionGateway")
logger.setLevel(logging.INFO)

# Optional MetaTrader 5 import with graceful fallback
try:
    import MetaTrader5 as mt5
    MT5_AVAILABLE = True
except ImportError:
    MT5_AVAILABLE = False
    mt5 = None

@dataclass
class OrderPlan:
    symbol: str
    action_type: str        # 'MARKET_BUY', 'MARKET_SELL', 'LIMIT_BUY', 'LIMIT_SELL'
    order_type_mt5: int
    volume: float
    entry_price: float
    stop_loss: float
    take_profit: float
    risk_reward_ratio: float
    risk_usd: float
    reason: str

@dataclass
class ExecutionReceipt:
    executed: bool
    order_id: Optional[int]
    symbol: str
    action: str
    price: float
    volume: float
    sl: float
    tp: float
    retcode: int
    status_message: str
    latency_ms: float
    rejection_gate: Optional[str] = None


class MT5ExecutionGateway:
    """
    Quantitative execution gateway enforcing Laya threshold gates and MT5 order transmission.
    """

    def __init__(self, risk_config: RiskConfig, simulation_mode: bool = False):
        self.risk = risk_config
        self.simulation_mode = simulation_mode or (not MT5_AVAILABLE)
        self._connected = False
        
        if not self.simulation_mode and MT5_AVAILABLE:
            self._connect_mt5()

    def _connect_mt5(self) -> bool:
        """Initializes connection to MT5 terminal."""
        if not MT5_AVAILABLE:
            self.simulation_mode = True
            return False

        # Attempt 1: Standard IPC connection
        if mt5.initialize():
            self._connected = True
            self.simulation_mode = False
            logger.info("Connected to MetaTrader 5 Terminal successfully.")
            return True

        # Attempt 2: Search standard executable paths
        candidates = [
            r"C:\MetaTrader5\terminal64.exe",
            r"C:\Program Files\MetaTrader 5\terminal64.exe",
            r"C:\Program Files (x86)\MetaTrader 5\terminal64.exe",
            os.path.expanduser(r"~\AppData\Local\Programs\MetaTrader 5\terminal64.exe")
        ]
        for cand in candidates:
            if os.path.exists(cand):
                if mt5.initialize(path=cand):
                    self._connected = True
                    self.simulation_mode = False
                    logger.info(f"Connected to MetaTrader 5 Terminal at {cand} successfully.")
                    return True

        logger.warning(f"MT5 initialize failed: {mt5.last_error()}. Defaulting to Simulation Mode.")
        self.simulation_mode = True
        return False

    def calculate_lot_size(
        self,
        symbol: str,
        account_equity: float,
        entry_price: float,
        stop_loss_price: float
    ) -> Tuple[float, float]:
        """
        Calculates position volume based on strict 1% account risk and SL distance.
        Formula: Lots = (Equity * RiskPct) / (SL_Distance * ContractSize)
        """
        risk_capital = account_equity * self.risk.risk_per_trade_pct
        sl_distance = abs(entry_price - stop_loss_price)
        
        if sl_distance <= 0:
            return 0.01, risk_capital

        # Symbol specifications
        if "XAU" in symbol:
            contract_size = 100.0   # 100 oz per standard lot
            point_value = 1.0       # $1 per $0.01 move per lot
            lot_step = 0.01
            min_lot = 0.01
            max_lot = 50.0
        elif "BTC" in symbol:
            contract_size = 1.0     # 1 BTC per lot
            point_value = 1.0
            lot_step = 0.01
            min_lot = 0.01
            max_lot = 10.0
        else:
            contract_size = 100000.0
            point_value = 10.0
            lot_step = 0.01
            min_lot = 0.01
            max_lot = 100.0

        loss_per_lot = sl_distance * contract_size
        if loss_per_lot <= 0:
            return min_lot, risk_capital

        raw_lots = risk_capital / loss_per_lot
        # Round down to lot step
        stepped_lots = int(raw_lots / lot_step) * lot_step
        final_lots = max(min_lot, min(max_lot, round(stepped_lots, 2)))
        
        return final_lots, risk_capital

    def evaluate_execution_gates(
        self,
        decision: LayaDecision,
        features: SMCFeatures,
        current_spread: float
    ) -> Tuple[bool, Optional[str]]:
        """
        Deterministic Multi-Tier Execution Filter:
        1. Action cannot be HOLD.
        2. Choice probability > 85% (0.85).
        3. Quality Score > 7.0/10.
        4. Spread must not exceed limit.
        5. Critical Noul validations must confirm confluence.
        """
        action = decision.choice.action
        prob = decision.choice.probability
        score = decision.score.grade

        # Gate 1: HOLD filtering
        if action == "HOLD":
            return False, "GATE_HOLD: Model selected HOLD"

        # Gate 2: Choice Probability > 85%
        if prob < self.risk.min_choice_confidence if hasattr(self.risk, 'min_choice_confidence') else prob < 0.85:
            return False, f"GATE_PROB: Choice prob {prob:.1%} < 85% threshold"

        # Gate 3: Score > 7.0 / 10
        if score <= 7.0:
            return False, f"GATE_SCORE: Setup score {score:.1f} <= 7.0/10"

        # Gate 4: Spread threshold check
        max_spread = (
            self.risk.max_spread_points_xau if "XAU" in features.symbol 
            else self.risk.max_spread_points_btc
        )
        if current_spread > max_spread:
            return False, f"GATE_SPREAD: Current spread {current_spread:.1f} > max {max_spread:.1f}"

        # Gate 5: Noul Hypotheses Validation
        # Check Asian sweep confirmation or FVG validity
        noul_checks = decision.noul_checks
        if "asian_sweep_confirmed" in noul_checks:
            asian_noul = noul_checks["asian_sweep_confirmed"]
            # If neither Asian liquidity was swept nor fresh FVG exists, fail
            fvg_noul = noul_checks.get("fvg_valid_untested")
            fvg_confirmed = fvg_noul.prob_true >= 0.70 if fvg_noul else False
            asian_confirmed = asian_noul.prob_true >= 0.65
            
            if not (asian_confirmed or fvg_confirmed):
                return False, "GATE_NOUL: Neither Asian sweep nor valid FVG confirmed by Laya Noul checks"

        return True, None

    def construct_order_plan(
        self,
        symbol: str,
        action: str,
        features: SMCFeatures,
        account_equity: float = 100000.0
    ) -> Optional[OrderPlan]:
        """
        Constructs order with institutional SL/TP levels anchored to SMC structures.
        """
        curr_price = features.current_price
        atr = features.atr_m5

        if action in ("MARKET_BUY", "LIMIT_BUY_ORDER_BLOCK"):
            # Stop Loss placement: below active OB low, FVG bottom, or recent dealing range low
            if features.active_m5_ob and features.active_m5_ob.direction == "BULLISH":
                sl = features.active_m5_ob.low - (0.2 * atr)
            elif features.active_m5_fvg and features.active_m5_fvg.direction == "BULLISH":
                sl = features.active_m5_fvg.bottom - (0.2 * atr)
            else:
                sl = features.dealing_range_low - (0.5 * atr)

            # Entry placement
            if action == "LIMIT_BUY_ORDER_BLOCK" and features.active_m5_ob:
                entry = features.active_m5_ob.high  # Mitigation entry
                order_type = 2 if not MT5_AVAILABLE else mt5.ORDER_TYPE_BUY_LIMIT
            else:
                entry = curr_price
                order_type = 0 if not MT5_AVAILABLE else mt5.ORDER_TYPE_BUY

            # Take Profit: BSL target or minimum 1:2.5 R:R
            sl_dist = abs(entry - sl)
            tp_target = max(features.nearest_bsl_target, entry + (sl_dist * 2.5))
            rr_ratio = (tp_target - entry) / max(sl_dist, 1e-4)

        elif action in ("MARKET_SELL", "LIMIT_SELL_ORDER_BLOCK"):
            # Stop Loss placement: above active OB high, FVG top, or dealing range high
            if features.active_m5_ob and features.active_m5_ob.direction == "BEARISH":
                sl = features.active_m5_ob.high + (0.2 * atr)
            elif features.active_m5_fvg and features.active_m5_fvg.direction == "BEARISH":
                sl = features.active_m5_fvg.top + (0.2 * atr)
            else:
                sl = features.dealing_range_high + (0.5 * atr)

            # Entry placement
            if action == "LIMIT_SELL_ORDER_BLOCK" and features.active_m5_ob:
                entry = features.active_m5_ob.low  # Mitigation entry
                order_type = 3 if not MT5_AVAILABLE else mt5.ORDER_TYPE_SELL_LIMIT
            else:
                entry = curr_price
                order_type = 1 if not MT5_AVAILABLE else mt5.ORDER_TYPE_SELL

            # Take Profit: SSL target or minimum 1:2.5 R:R
            sl_dist = abs(sl - entry)
            tp_target = min(features.nearest_ssl_target, entry - (sl_dist * 2.5))
            rr_ratio = (entry - tp_target) / max(sl_dist, 1e-4)

        else:
            return None

        # Filter minimum R:R
        if rr_ratio < self.risk.min_risk_reward_ratio:
            logger.info(f"Filtered setup on R:R: {rr_ratio:.2f} < {self.risk.min_risk_reward_ratio}")
            return None

        volume, risk_usd = self.calculate_lot_size(symbol, account_equity, entry, sl)

        return OrderPlan(
            symbol=symbol,
            action_type=action,
            order_type_mt5=order_type,
            volume=volume,
            entry_price=round(entry, 2),
            stop_loss=round(sl, 2),
            take_profit=round(tp_target, 2),
            risk_reward_ratio=round(rr_ratio, 2),
            risk_usd=round(risk_usd, 2),
            reason=f"SMC_{action}_CONFIRMED"
        )

    def route_execution(
        self,
        decision: LayaDecision,
        features: SMCFeatures,
        account_equity: float = 100000.0
    ) -> ExecutionReceipt:
        """
        Primary execution routing function evaluating thresholds and triggering MT5 order.
        """
        t0 = time.perf_counter()
        symbol = features.symbol

        # 1. Evaluate Decision & Gating Thresholds
        passed_gates, rejection_gate = self.evaluate_execution_gates(
            decision=decision,
            features=features,
            current_spread=features.spread_points
        )

        if not passed_gates:
            elapsed_ms = (time.perf_counter() - t0) * 1000.0
            return ExecutionReceipt(
                executed=False,
                order_id=None,
                symbol=symbol,
                action=decision.choice.action,
                price=features.current_price,
                volume=0.0,
                sl=0.0,
                tp=0.0,
                retcode=-1,
                status_message=f"REJECTED: {rejection_gate}",
                latency_ms=round(elapsed_ms, 2),
                rejection_gate=rejection_gate
            )

        # 2. Build Invalidation-Anchored Order Plan
        plan = self.construct_order_plan(
            symbol=symbol,
            action=decision.choice.action,
            features=features,
            account_equity=account_equity
        )

        if plan is None:
            elapsed_ms = (time.perf_counter() - t0) * 1000.0
            return ExecutionReceipt(
                executed=False,
                order_id=None,
                symbol=symbol,
                action=decision.choice.action,
                price=features.current_price,
                volume=0.0,
                sl=0.0,
                tp=0.0,
                retcode=-2,
                status_message="REJECTED: Risk/Reward ratio or lot sizing condition not satisfied",
                latency_ms=round(elapsed_ms, 2),
                rejection_gate="GATE_RR_OR_LOTS"
            )

        # 3. Transmit Order to MT5 Terminal
        if self.simulation_mode or not MT5_AVAILABLE:
            # Paper execution receipt
            sim_order_id = int(time.time() * 1000) % 10000000
            elapsed_ms = (time.perf_counter() - t0) * 1000.0
            logger.info(
                f"[SIMULATION EXECUTION] {plan.action_type} {plan.volume} lots {symbol} @ {plan.entry_price} "
                f"SL:{plan.stop_loss} TP:{plan.take_profit} (R:R {plan.risk_reward_ratio}:1, Risk: ${plan.risk_usd})"
            )
            return ExecutionReceipt(
                executed=True,
                order_id=sim_order_id,
                symbol=symbol,
                action=plan.action_type,
                price=plan.entry_price,
                volume=plan.volume,
                sl=plan.stop_loss,
                tp=plan.take_profit,
                retcode=10009,  # TRADE_RETCODE_DONE
                status_message="SUCCESS_SIMULATED_ORDER_FILLED",
                latency_ms=round(elapsed_ms, 2)
            )

        # Live MT5 Order Dispatch
        is_pending = "LIMIT" in plan.action_type
        trade_action = mt5.TRADE_ACTION_PENDING if is_pending else mt5.TRADE_ACTION_DEAL

        # Fetch fresh tick to ensure precise market entry price
        tick = mt5.symbol_info_tick(symbol)
        if not is_pending and tick:
            exec_price = tick.ask if plan.order_type_mt5 == mt5.ORDER_TYPE_BUY else tick.bid
        else:
            exec_price = plan.entry_price

        # Detect supported broker filling mode
        sym_info = mt5.symbol_info(symbol)
        filling_mode = sym_info.filling_mode if sym_info else 0
        if filling_mode & 2:
            type_filling = mt5.ORDER_FILLING_IOC
        elif filling_mode & 1:
            type_filling = mt5.ORDER_FILLING_FOK
        else:
            type_filling = mt5.ORDER_FILLING_RETURN

        request = {
            "action": trade_action,
            "symbol": symbol,
            "volume": plan.volume,
            "type": plan.order_type_mt5,
            "price": exec_price,
            "sl": plan.stop_loss,
            "tp": plan.take_profit,
            "deviation": self.risk.slippage_points,
            "magic": 999111,  # SparkX EA magic number
            "comment": f"SparkX_Laya_S{int(decision.score.grade)}",
            "type_time": mt5.ORDER_TIME_GTC,
            "type_filling": type_filling,
        }

        result = mt5.order_send(request)
        elapsed_ms = (time.perf_counter() - t0) * 1000.0

        if result is None or result.retcode != mt5.TRADE_RETCODE_DONE:
            error_code = result.retcode if result else mt5.last_error()
            err_msg = f"MT5 Execution Failed with code {error_code}: {result.comment if result else ''}"
            logger.error(err_msg)
            return ExecutionReceipt(
                executed=False,
                order_id=None,
                symbol=symbol,
                action=plan.action_type,
                price=exec_price,
                volume=plan.volume,
                sl=plan.stop_loss,
                tp=plan.take_profit,
                retcode=error_code,
                status_message=err_msg,
                latency_ms=round(elapsed_ms, 2),
                rejection_gate="MT5_TERMINAL_ERROR"
            )

        logger.info(f"[LIVE MT5 FILLED] Ticket #{result.order} {plan.action_type} {plan.volume} {symbol} @ {result.price}")
        return ExecutionReceipt(
            executed=True,
            order_id=result.order,
            symbol=symbol,
            action=plan.action_type,
            price=result.price,
            volume=plan.volume,
            sl=plan.stop_loss,
            tp=plan.take_profit,
            retcode=result.retcode,
            status_message="SUCCESS_LIVE_ORDER_FILLED",
            latency_ms=round(elapsed_ms, 2)
        )

    def execute_live_order(
        self,
        symbol: str,
        action: str,  # 'BUY', 'SELL', 'MARKET_BUY', 'MARKET_SELL'
        sl_points: float = 4.0,   # default $4.00 SL on Gold
        tp_points: float = 10.0,  # default $10.00 TP on Gold (2.5:1 R:R)
        lot_size: Optional[float] = None,
        comment: str = "SparkX_Live"
    ) -> ExecutionReceipt:
        """
        Direct live market order dispatch to logged-in MetaTrader 5 terminal.
        """
        t0 = time.perf_counter()
        if not MT5_AVAILABLE or not mt5.initialize():
            return ExecutionReceipt(
                executed=False, order_id=None, symbol=symbol, action=action,
                price=0.0, volume=0.0, sl=0.0, tp=0.0, retcode=-1,
                status_message="MT5 not available or failed to initialize",
                latency_ms=0.0, rejection_gate="MT5_NOT_AVAILABLE"
            )

        mt5.symbol_select(symbol, True)
        tick = mt5.symbol_info_tick(symbol)
        if not tick:
            return ExecutionReceipt(
                executed=False, order_id=None, symbol=symbol, action=action,
                price=0.0, volume=0.0, sl=0.0, tp=0.0, retcode=-1,
                status_message=f"Failed to fetch tick for {symbol}",
                latency_ms=0.0, rejection_gate="MT5_NO_TICK"
            )

        is_buy = "BUY" in action.upper()
        order_type = mt5.ORDER_TYPE_BUY if is_buy else mt5.ORDER_TYPE_SELL
        exec_price = round(tick.ask if is_buy else tick.bid, 2)
        sl_price = round(exec_price - sl_points if is_buy else exec_price + sl_points, 2)
        tp_price = round(exec_price + tp_points if is_buy else exec_price - tp_points, 2)

        # Dynamic 1% risk lot sizing if not explicitly passed
        if lot_size is None:
            acc = mt5.account_info()
            equity = acc.equity if acc else 10000.0
            lot_size, _ = self.calculate_lot_size(symbol, equity, exec_price, sl_price)

        sym_info = mt5.symbol_info(symbol)
        filling_mode = sym_info.filling_mode if sym_info else 0
        if filling_mode & 2:
            type_filling = mt5.ORDER_FILLING_IOC
        elif filling_mode & 1:
            type_filling = mt5.ORDER_FILLING_FOK
        else:
            type_filling = mt5.ORDER_FILLING_RETURN

        request = {
            "action": mt5.TRADE_ACTION_DEAL,
            "symbol": symbol,
            "volume": float(lot_size),
            "type": order_type,
            "price": exec_price,
            "sl": sl_price,
            "tp": tp_price,
            "deviation": self.risk.slippage_points,
            "magic": 999111,
            "comment": comment,
            "type_time": mt5.ORDER_TIME_GTC,
            "type_filling": type_filling,
        }

        result = mt5.order_send(request)
        elapsed_ms = (time.perf_counter() - t0) * 1000.0

        if result is None or result.retcode != mt5.TRADE_RETCODE_DONE:
            error_code = result.retcode if result else mt5.last_error()
            err_msg = f"MT5 Execution Failed (retcode {error_code}): {result.comment if result else ''}"
            logger.error(err_msg)
            return ExecutionReceipt(
                executed=False, order_id=None, symbol=symbol, action=action,
                price=exec_price, volume=lot_size, sl=sl_price, tp=tp_price,
                retcode=error_code, status_message=err_msg,
                latency_ms=round(elapsed_ms, 2), rejection_gate="MT5_TERMINAL_ERROR"
            )

        logger.info(f"[LIVE MT5 FILLED] Ticket #{result.order} {action} {lot_size} lots {symbol} @ {result.price}")
        return ExecutionReceipt(
            executed=True, order_id=result.order, symbol=symbol, action=action,
            price=result.price, volume=lot_size, sl=sl_price, tp=tp_price,
            retcode=result.retcode, status_message="SUCCESS_LIVE_ORDER_FILLED",
            latency_ms=round(elapsed_ms, 2)
        )

    def close_all_positions(self, symbol: Optional[str] = None) -> List[int]:
        """Closes all open positions on the account or for a specific symbol."""
        if not MT5_AVAILABLE or not mt5.initialize():
            return []
        positions = mt5.positions_get(symbol=symbol) if symbol else mt5.positions_get()
        closed_tickets = []
        for pos in (positions or []):
            order_type = mt5.ORDER_TYPE_SELL if pos.type == mt5.ORDER_TYPE_BUY else mt5.ORDER_TYPE_BUY
            tick = mt5.symbol_info_tick(pos.symbol)
            if not tick:
                continue
            price = tick.bid if pos.type == mt5.ORDER_TYPE_BUY else tick.ask
            sym_info = mt5.symbol_info(pos.symbol)
            filling_mode = sym_info.filling_mode if sym_info else 0
            type_filling = mt5.ORDER_FILLING_IOC if (filling_mode & 2) else (mt5.ORDER_FILLING_FOK if (filling_mode & 1) else mt5.ORDER_FILLING_RETURN)

            request = {
                "action": mt5.TRADE_ACTION_DEAL,
                "position": pos.ticket,
                "symbol": pos.symbol,
                "volume": pos.volume,
                "type": order_type,
                "price": price,
                "deviation": 20,
                "magic": 999111,
                "comment": "SparkX_Close",
                "type_time": mt5.ORDER_TIME_GTC,
                "type_filling": type_filling
            }
            res = mt5.order_send(request)
            if res and res.retcode == mt5.TRADE_RETCODE_DONE:
                closed_tickets.append(pos.ticket)
                logger.info(f"Closed position #{pos.ticket} for {pos.symbol}")
        return closed_tickets

    def close_position_by_ticket(self, ticket: int) -> bool:
        """Closes a specific open position by its ticket number."""
        if not MT5_AVAILABLE or not mt5.initialize():
            return False
        positions = mt5.positions_get(ticket=ticket)
        if not positions:
            return False
        pos = positions[0]
        order_type = mt5.ORDER_TYPE_SELL if pos.type == mt5.ORDER_TYPE_BUY else mt5.ORDER_TYPE_BUY
        tick = mt5.symbol_info_tick(pos.symbol)
        if not tick:
            return False
        price = tick.bid if pos.type == mt5.ORDER_TYPE_BUY else tick.ask
        sym_info = mt5.symbol_info(pos.symbol)
        filling_mode = sym_info.filling_mode if sym_info else 0
        type_filling = mt5.ORDER_FILLING_IOC if (filling_mode & 2) else (mt5.ORDER_FILLING_FOK if (filling_mode & 1) else mt5.ORDER_FILLING_RETURN)

        request = {
            "action": mt5.TRADE_ACTION_DEAL,
            "position": pos.ticket,
            "symbol": pos.symbol,
            "volume": pos.volume,
            "type": order_type,
            "price": price,
            "deviation": 20,
            "magic": 999111,
            "comment": "SparkX_Ticket_Close",
            "type_time": mt5.ORDER_TIME_GTC,
            "type_filling": type_filling
        }
        res = mt5.order_send(request)
        if res and res.retcode == mt5.TRADE_RETCODE_DONE:
            logger.info(f"Successfully closed MT5 position #{ticket} for {pos.symbol} @ {price}")
            return True
        else:
            err = res.retcode if res else mt5.last_error()
            logger.warning(f"Failed to close position #{ticket}: error code {err}")
            return False

