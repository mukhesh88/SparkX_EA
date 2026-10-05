"""
SparkX EA - Trade Manager & Execution State Tracker
Maintains live open positions, historical closed trades, PnL statistics,
and dynamic trading settings with JSON persistence.
"""

import os
import json
import time
import logging
from datetime import datetime, timezone, timedelta
from typing import Dict, List, Any, Optional, Tuple

from src.sl_analyzer import SLDiagnosticEngine, SLPostMortem
from src.online_learner import online_learner

try:
    import MetaTrader5 as mt5
    MT5_AVAILABLE = True
except ImportError:
    MT5_AVAILABLE = False
    mt5 = None

logger = logging.getLogger("SparkX.TradeManager")

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SETTINGS_PATH = os.path.join(ROOT_DIR, "config", "trade_settings.json")
HISTORY_PATH = os.path.join(ROOT_DIR, "config", "trade_history.json")


def is_same_financial_asset(sym1: str, sym2: str) -> bool:
    """Matches base symbols regardless of broker suffixes (e.g. XAUUSD == XAUUSDm == GOLD)."""
    s1 = str(sym1).upper().strip()
    s2 = str(sym2).upper().strip()
    if not s1 or not s2:
        return False
    if s1 == s2:
        return True
    is_gold1 = "XAU" in s1 or "GOLD" in s1
    is_gold2 = "XAU" in s2 or "GOLD" in s2
    if is_gold1 and is_gold2:
        return True
    is_btc1 = "BTC" in s1
    is_btc2 = "BTC" in s2
    if is_btc1 and is_btc2:
        return True
    # Strip common broker suffixes
    base1 = s1.split('.')[0].rstrip('m_ic')
    base2 = s2.split('.')[0].rstrip('m_ic')
    return base1 == base2


class TradeManager:
    """Manages active trade state, historical fills, settings, and PnL."""

    def __init__(self):
        self.settings: Dict[str, Any] = self._load_settings()
        self.simulated_positions: List[Dict[str, Any]] = []
        self.history: List[Dict[str, Any]] = self._load_history()
        self._next_sim_ticket = 70001000
        self.last_trade_time: float = 0.0
        self.last_closed_action: str = ""
        self.last_closed_outcome: str = ""
        self.last_closed_time: float = 0.0
        self.last_closed_price: float = 0.0
        self.last_closed_symbol: str = ""

        # Continuous Learning & SL Root Cause Tracking
        self.trade_snapshots: Dict[int, Dict[str, Any]] = {}
        self.tracked_live_tickets: set = set()
        self.adaptive_root_cause: Dict[str, str] = {}
        self.adaptive_rectifications: Dict[str, str] = {}
        self.last_post_mortem: Dict[str, Any] = {}

    def _load_settings(self) -> Dict[str, Any]:
        defaults = {
            "auto_trade_enabled": True,
            "lot_size_mode": "FIXED",
            "fixed_lot_size": 0.10,
            "risk_per_trade_pct": 1.0,
            "sl_points": 4.5,
            "tp_points": 12.0,
            "max_open_positions": 2,
            "max_spread_points": 25.0,
            "min_setup_score": 7.0,
            "min_confidence_pct": 85.0,
            "require_h1_trend": True,
            "require_m5_fvg": True,
            "require_liquidity_sweep": false_flag if "false_flag" in locals() else False,
            "discord_alerts": True,
            "telegram_alerts": False
        }
        if os.path.exists(SETTINGS_PATH):
            try:
                with open(SETTINGS_PATH, "r", encoding="utf-8") as f:
                    data = json.load(f)
                    defaults.update(data)
            except Exception as e:
                logger.warning(f"Failed to load {SETTINGS_PATH}: {e}")
        return defaults

    def save_settings(self, new_settings: Dict[str, Any]) -> bool:
        """Saves updated settings to disk and active instance."""
        try:
            self.settings.update(new_settings)
            os.makedirs(os.path.dirname(SETTINGS_PATH), exist_ok=True)
            with open(SETTINGS_PATH, "w", encoding="utf-8") as f:
                json.dump(self.settings, f, indent=2)
            logger.info("Trade settings successfully saved to config/trade_settings.json")
            return True
        except Exception as e:
            logger.error(f"Error saving trade settings: {e}")
            return False

    def _load_history(self) -> List[Dict[str, Any]]:
        if os.path.exists(HISTORY_PATH):
            try:
                with open(HISTORY_PATH, "r", encoding="utf-8") as f:
                    return json.load(f)
            except Exception as e:
                logger.warning(f"Failed to load {HISTORY_PATH}: {e}")
        return []

    def _save_history(self):
        try:
            os.makedirs(os.path.dirname(HISTORY_PATH), exist_ok=True)
            with open(HISTORY_PATH, "w", encoding="utf-8") as f:
                json.dump(self.history[:100], f, indent=2)
        except Exception as e:
            logger.error(f"Error saving history: {e}")

    def add_simulated_position(
        self,
        symbol: str,
        action: str,
        price: float,
        volume: float,
        sl: float,
        tp: float,
        comment: str = "SparkX_Sim"
    ) -> int:
        ticket = self._next_sim_ticket
        self._next_sim_ticket += 1
        pos = {
            "ticket": ticket,
            "time": int(time.time()),
            "time_str": datetime.now(timezone.utc).strftime("%H:%M:%S"),
            "symbol": symbol,
            "type": "BUY" if "BUY" in action.upper() else "SELL",
            "volume": round(volume, 2),
            "price_open": round(price, 2),
            "price_current": round(price, 2),
            "sl": round(sl, 2),
            "tp": round(tp, 2),
            "profit": 0.0,
            "comment": comment
        }
        self.simulated_positions.append(pos)
        self.last_trade_time = time.time()

        # Record entry snapshot for forensic post-mortem
        self.trade_snapshots[ticket] = {
            "ticket": ticket,
            "symbol": symbol,
            "direction": pos["type"],
            "open_time": time.time(),
            "entry_price": float(price),
            "sl_price": float(sl),
            "tp_price": float(tp),
            "volume": float(volume),
            "compressed_state": comment if comment.startswith("<MKT_STATE>") else "",
            "features": {},
            "decision": {},
            "min_price": float(price),
            "max_price": float(price)
        }

        logger.info(f"Added simulated position #{ticket} {action} {volume} {symbol} @ {price}")
        return ticket

    def record_trade_snapshot(
        self,
        ticket: int,
        symbol: str,
        action: str,
        price: float,
        sl: float,
        tp: float,
        compressed_state: str = "",
        features: Optional[Dict[str, Any]] = None,
        decision: Optional[Dict[str, Any]] = None
    ):
        """Saves a rich entry flight-recorder snapshot for post-mortem forensics."""
        self.trade_snapshots[ticket] = {
            "ticket": ticket,
            "symbol": symbol,
            "direction": "BUY" if "BUY" in action.upper() else "SELL",
            "open_time": time.time(),
            "entry_price": float(price),
            "sl_price": float(sl),
            "tp_price": float(tp),
            "compressed_state": compressed_state,
            "features": features or {},
            "decision": decision or {},
            "min_price": float(price),
            "max_price": float(price)
        }
        logger.info(f"Latched flight-recorder snapshot for #{ticket} {action} {symbol} @ ${price:.2f}")

    def record_trade_executed(self, symbol: str, action: str, price: float):
        """Records the timestamp and attributes of an executed trade."""
        self.last_trade_time = time.time()
        logger.info(f"Institutional Trade Latched: {symbol} {action} @ ${price:.2f} at {self.last_trade_time}")

    def close_simulated_position(self, ticket: int, current_price: float) -> Optional[Dict[str, Any]]:
        target_idx = -1
        for idx, pos in enumerate(self.simulated_positions):
            if pos["ticket"] == ticket:
                target_idx = idx
                break
        if target_idx == -1:
            return None

        pos = self.simulated_positions.pop(target_idx)
        is_buy = pos["type"] == "BUY"
        # For gold, 1 point ($1) with 0.10 lots = $10 profit (contract size 100)
        pnl = (current_price - pos["price_open"]) * pos["volume"] * 100.0 if is_buy else \
              (pos["price_open"] - current_price) * pos["volume"] * 100.0

        hist_item = {
            "ticket": pos["ticket"],
            "time": int(time.time()),
            "time_str": datetime.now(timezone.utc).strftime("%m-%d %H:%M"),
            "symbol": pos["symbol"],
            "type": pos["type"],
            "volume": pos["volume"],
            "price_open": pos["price_open"],
            "price_close": round(current_price, 2),
            "profit": round(pnl, 2),
            "outcome": "WIN" if pnl >= 0 else "LOSS",
            "comment": pos["comment"]
        }
        self.history.insert(0, hist_item)
        self._save_history()

        # Update Institutional Trade Lifecycle telemetry
        self.last_closed_action = pos["type"]
        self.last_closed_outcome = hist_item["outcome"]
        self.last_closed_time = time.time()
        self.last_closed_price = float(current_price)
        self.last_closed_symbol = pos["symbol"]

        # Check for SL / Loss post-mortem and auto-retraining
        snap = self.trade_snapshots.pop(ticket, None)
        if hist_item["outcome"] == "LOSS":
            entry_state = snap.get("compressed_state", "") if snap else ""
            if not entry_state:
                entry_state = f"<MKT_STATE> SYM:{pos['symbol']} | DIR:{pos['type']} | SL_HIT | EXIT:{current_price:.2f} </MKT_STATE>"

            f_entry = snap.get("features", {}) if snap else {}
            min_px = snap.get("min_price", min(pos["price_open"], current_price)) if snap else None
            max_px = snap.get("max_price", max(pos["price_open"], current_price)) if snap else None

            post_mortem = SLDiagnosticEngine.diagnose(
                ticket=ticket,
                symbol=pos["symbol"],
                direction=pos["type"],
                open_time=float(pos.get("time", time.time())),
                close_time=time.time(),
                entry_price=pos["price_open"],
                exit_price=float(current_price),
                sl_price=pos["sl"],
                tp_price=pos["tp"],
                pnl=pnl,
                compressed_state=entry_state,
                features_at_entry=f_entry,
                min_price_during_trade=min_px,
                max_price_during_trade=max_px
            )
            self.last_post_mortem[pos["symbol"]] = post_mortem.to_dict()
            self.adaptive_root_cause[pos["symbol"]] = post_mortem.root_cause
            self.adaptive_rectifications[pos["symbol"]] = post_mortem.rectification_rule

            # Asynchronously queue for real-time model retraining
            online_learner.enqueue_sl_event(post_mortem)
        elif hist_item["outcome"] == "WIN" and snap:
            if snap.get("compressed_state"):
                online_learner.register_positive_trade(
                    snap["compressed_state"],
                    choice=0 if pos["type"] == "BUY" else 1,
                    score=9.2,
                    noul=[1.0, 1.0, 1.0]
                )

        logger.info(f"Closed simulated position #{ticket} @ ${current_price:.2f} | PnL: ${pnl:.2f} ({hist_item['outcome']})")
        return hist_item

    def can_enter_trade(
        self,
        symbol: str,
        action: str,
        price: float,
        open_positions: List[Dict[str, Any]],
        features: Optional[Dict[str, Any]] = None
    ) -> Tuple[bool, str]:
        """
        Institutional Risk, Anti-Chasing & Self-Correcting Adaptive Gate:
        Enforces professional discipline:
          1. Maximum 1 active position per symbol (no duplicate stacking).
          2. Minimum 180s setup spacing cooldown.
          3. Post-TP Anti-Chasing Hysteresis (blocks buying the top after TP).
          4. Post-SL Anti-Revenge Lockout (15-min cooldown after stop-loss).
          5. Adaptive Rectification Gate (enforces specific SMC lesson learned from previous SL).
        """
        now = time.time()
        act_upper = "BUY" if "BUY" in action.upper() else "SELL"

        # 1. Check auto trade setting
        if not self.settings.get("auto_trade_enabled", True):
            return False, "Auto-trading is disabled in settings"

        # 2. Maximum 1 active position per asset (strictly eliminates duplicate orders and stacking)
        symbol_pos = [p for p in open_positions if is_same_financial_asset(p.get("symbol", ""), symbol)]
        if len(symbol_pos) >= 1:
            ticket = symbol_pos[0].get('ticket', 'N/A')
            sym_name = symbol_pos[0].get('symbol', symbol)
            return False, f"Position already active for asset {symbol} (Ticket #{ticket} on {sym_name}). Max allowed: 1 per asset."

        max_allowed = int(self.settings.get("max_open_positions", 1))
        if len(open_positions) >= max_allowed:
            return False, f"Maximum total portfolio positions reached ({len(open_positions)}/{max_allowed})"

        # 3. Minimum setup spacing cooldown (minimum 180 seconds / 3 mins)
        if (now - self.last_trade_time) < 180.0:
            remaining = int(180.0 - (now - self.last_trade_time))
            return False, f"Trade pacing cooldown active ({remaining}s remaining before next setup)"

        # 4. Post-TP Anti-Chasing Hysteresis (Never buy the high where TP was just filled!)
        if self.last_closed_symbol == symbol and self.last_closed_outcome == "WIN":
            elapsed = now - self.last_closed_time
            if elapsed < 600.0:  # 10 minutes lockout after TP
                if act_upper == "BUY" and self.last_closed_action == "BUY":
                    pullback_dist = self.last_closed_price - price
                    if pullback_dist < 5.0:
                        return False, (
                            f"Post-TP Anti-Chasing Lockout: Price (${price:.2f}) has not retraced "
                            f"at least $5.00 below recent TP exit (${self.last_closed_price:.2f}). "
                            f"Pullback so far: ${pullback_dist:.2f}. Waiting for market retracement."
                        )
                elif act_upper == "SELL" and self.last_closed_action == "SELL":
                    pullback_dist = price - self.last_closed_price
                    if pullback_dist < 5.0:
                        return False, (
                            f"Post-TP Anti-Chasing Lockout: Price (${price:.2f}) has not retraced "
                            f"at least $5.00 above recent TP exit (${self.last_closed_price:.2f}). "
                            f"Pullback so far: ${pullback_dist:.2f}. Waiting for market retracement."
                        )

        # 5. Post-SL Anti-Revenge Lockout (15 minutes after Stop-Loss in same direction)
        if self.last_closed_symbol == symbol and self.last_closed_outcome == "LOSS":
            elapsed = now - self.last_closed_time
            if elapsed < 900.0 and self.last_closed_action == act_upper:
                remaining = int(900.0 - elapsed)
                return False, f"Post-SL Anti-Revenge Lockout: 15-min cooldown active after {act_upper} loss ({remaining}s remaining)"

        # 6. Adaptive SL Rectification Gate:
        # If previous trade hit SL, enforce the specific forensic lesson learned from the post-mortem
        if symbol in self.adaptive_root_cause and features:
            cause = self.adaptive_root_cause[symbol]
            if cause == "HTF_TREND_CONFLICT":
                h1_trend = str(features.get("h1_trend", "")).upper()
                if act_upper == "BUY" and ("BEAR" in h1_trend or "RANGE" in h1_trend):
                    return False, f"Adaptive SL Gate: Previous SL due to HTF Trend Conflict. BUY blocked while H1 is {h1_trend}."
                if act_upper == "SELL" and ("BULL" in h1_trend or "RANGE" in h1_trend):
                    return False, f"Adaptive SL Gate: Previous SL due to HTF Trend Conflict. SELL blocked while H1 is {h1_trend}."
            elif cause == "LIQUIDITY_HUNT_SWEEP":
                if act_upper == "BUY" and not features.get("ssl_swept", False):
                    return False, "Adaptive SL Gate: Previous SL due to premature entry before SSL hunt. Awaiting verified sweep below Asian Low."
                if act_upper == "SELL" and not features.get("bsl_swept", False):
                    return False, "Adaptive SL Gate: Previous SL due to premature entry before BSL hunt. Awaiting verified sweep above Asian High."
            elif cause == "PREMIUM_DISCOUNT_VIOLATION":
                zone = str(features.get("price_zone", features.get("zone", ""))).upper()
                if act_upper == "BUY" and "DISCOUNT" not in zone:
                    return False, f"Adaptive SL Gate: Previous SL due to Pricing Violation. BUY blocked in {zone} zone."
                if act_upper == "SELL" and "PREMIUM" not in zone:
                    return False, f"Adaptive SL Gate: Previous SL due to Pricing Violation. SELL blocked in {zone} zone."
            elif cause == "FVG_INVERSION_FAILURE":
                disp = features.get("displacement", False)
                if not disp:
                    return False, "Adaptive SL Gate: Previous SL due to FVG Inversion. Institutional displacement required before taking next FVG setup."

        return True, "PASSED"

    def get_positions(self, is_live_broker: bool, current_price: float, symbol: str = "XAUUSD") -> List[Dict[str, Any]]:
        """Returns live active positions from MT5 or the simulated tracker."""
        if is_live_broker and MT5_AVAILABLE and mt5 is not None:
            # Query all open positions across the MT5 account to prevent missing broker-suffixed symbols
            raw_pos = mt5.positions_get() or ()
            positions = []
            current_live_tickets = set()
            for p in raw_pos:
                t = int(p.ticket)
                current_live_tickets.add(t)
                positions.append({
                    "ticket": t,
                    "time": int(p.time),
                    "time_str": datetime.fromtimestamp(p.time, timezone.utc).strftime("%H:%M:%S"),
                    "symbol": p.symbol,
                    "type": "BUY" if p.type == 0 else "SELL",
                    "volume": round(p.volume, 2),
                    "price_open": round(p.price_open, 2),
                    "price_current": round(p.price_current, 2),
                    "sl": round(p.sl, 2),
                    "tp": round(p.tp, 2),
                    "profit": round(p.profit, 2),
                    "comment": str(p.comment)
                })
                # Update price envelopes in flight-recorder snapshot
                if t in self.trade_snapshots:
                    snap = self.trade_snapshots[t]
                    cur_p = float(p.price_current)
                    snap["min_price"] = min(snap.get("min_price", cur_p), cur_p)
                    snap["max_price"] = max(snap.get("max_price", cur_p), cur_p)

            # Check for live deals closed via SL
            closed_live_tickets = self.tracked_live_tickets - current_live_tickets
            for ct in closed_live_tickets:
                self._check_and_process_live_deal_closure(ct)

            self.tracked_live_tickets = current_live_tickets
            return positions

        # Simulation mode: update floating PnL and track price extremes
        updated = []
        to_close = []
        for p in self.simulated_positions:
            is_buy = p["type"] == "BUY"
            px = current_price if current_price > 0 else p["price_open"]
            p["price_current"] = round(px, 2)
            pnl = (px - p["price_open"]) * p["volume"] * 100.0 if is_buy else \
                  (p["price_open"] - px) * p["volume"] * 100.0
            p["profit"] = round(pnl, 2)

            # Track price envelope in flight-recorder snapshot
            if p["ticket"] in self.trade_snapshots:
                snap = self.trade_snapshots[p["ticket"]]
                snap["min_price"] = min(snap.get("min_price", px), px)
                snap["max_price"] = max(snap.get("max_price", px), px)

            # Check SL / TP trigger
            if is_buy:
                if p["sl"] > 0 and px <= p["sl"]:
                    to_close.append((p["ticket"], p["sl"]))
                elif p["tp"] > 0 and px >= p["tp"]:
                    to_close.append((p["ticket"], p["tp"]))
            else:
                if p["sl"] > 0 and px >= p["sl"]:
                    to_close.append((p["ticket"], p["sl"]))
                elif p["tp"] > 0 and px <= p["tp"]:
                    to_close.append((p["ticket"], p["tp"]))

            updated.append(p)

        for ticket, trigger_px in to_close:
            self.close_simulated_position(ticket, trigger_px)

        return [p for p in updated if p["ticket"] not in [t[0] for t in to_close]]

    def _check_and_process_live_deal_closure(self, ticket: int):
        """Forensic inspection of deals in MT5 history when a live position closes."""
        if not (MT5_AVAILABLE and mt5 is not None):
            return
        now_dt = datetime.now(timezone.utc)
        deals = mt5.history_deals_get(now_dt - timedelta(hours=2), now_dt)
        if not deals:
            return
        for d in reversed(deals):
            if d.position_id == ticket and (d.entry in (1, 2) or d.profit != 0):
                is_loss = d.profit < 0 or "[sl]" in str(d.comment).lower()
                snap = self.trade_snapshots.pop(ticket, None)
                if is_loss:
                    entry_state = snap.get("compressed_state", "") if snap else f"<MKT_STATE> SYM:{d.symbol} | LIVE_SL_HIT </MKT_STATE>"
                    post_mortem = SLDiagnosticEngine.diagnose(
                        ticket=ticket,
                        symbol=d.symbol,
                        direction="BUY" if d.type == 1 else "SELL",
                        open_time=snap.get("open_time", d.time - 300) if snap else (d.time - 300),
                        close_time=float(d.time),
                        entry_price=snap.get("entry_price", d.price) if snap else d.price,
                        exit_price=float(d.price),
                        sl_price=snap.get("sl_price", d.price) if snap else d.price,
                        tp_price=snap.get("tp_price", d.price) if snap else d.price,
                        pnl=float(d.profit),
                        compressed_state=entry_state,
                        features_at_entry=snap.get("features", {}) if snap else {},
                        min_price_during_trade=snap.get("min_price") if snap else None,
                        max_price_during_trade=snap.get("max_price") if snap else None
                    )
                    self.last_post_mortem[d.symbol] = post_mortem.to_dict()
                    self.adaptive_root_cause[d.symbol] = post_mortem.root_cause
                    self.adaptive_rectifications[d.symbol] = post_mortem.rectification_rule
                    online_learner.enqueue_sl_event(post_mortem)
                    logger.warning(f"[LIVE MT5 SL DETECTED] #{ticket} closed at loss ${d.profit:.2f}. Queued auto-retraining.")
                break

    def get_history(self, is_live_broker: bool) -> List[Dict[str, Any]]:
        """Returns completed trades merged from MT5 deals and local journal."""
        if is_live_broker and MT5_AVAILABLE and mt5 is not None:
            now_dt = datetime.now(timezone.utc)
            from_dt = now_dt - timedelta(days=90)
            deals = mt5.history_deals_get(from_dt, now_dt)
            if deals:
                live_deals = []
                for d in reversed(deals):
                    # Only entry OUT (closed deals) or deals with realized profit
                    if d.entry in (1, 2) or (d.profit != 0 and d.symbol):
                        open_px = float(d.price)
                        pos_deals = mt5.history_deals_get(position=d.position_id)
                        if pos_deals and len(pos_deals) > 0:
                            open_px = float(pos_deals[0].price)

                        # Determine original trade direction
                        action = "BUY" if d.type == 0 else "SELL"
                        if d.entry == 1:
                            action = "SELL" if d.type == 0 else "BUY"

                        comment_str = str(d.comment).strip()
                        if not comment_str:
                            comment_str = "Take Profit Hit" if d.profit > 0 else "Market Exit"

                        live_deals.append({
                            "ticket": int(d.ticket),
                            "position_id": int(d.position_id),
                            "time": int(d.time),
                            "time_str": datetime.fromtimestamp(d.time, timezone.utc).strftime("%m-%d %H:%M"),
                            "symbol": d.symbol,
                            "type": action,
                            "volume": round(float(d.volume), 2),
                            "price_open": round(open_px, 2),
                            "price_close": round(float(d.price), 2),
                            "profit": round(float(d.profit), 2),
                            "outcome": "WIN" if d.profit >= 0 else "LOSS",
                            "comment": comment_str
                        })
                        if len(live_deals) >= 60:
                            break
                if live_deals:
                    self.history = live_deals
                    self._save_history()
                    return live_deals

        # Filter out test artifacts
        filtered = [h for h in self.history if "Test" not in str(h.get("comment", ""))][:50]
        return filtered


trade_manager = TradeManager()
