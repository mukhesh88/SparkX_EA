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
from typing import Dict, List, Any, Optional

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
        logger.info(f"Added simulated position #{ticket} {action} {volume} {symbol} @ {price}")
        return ticket

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

        logger.info(f"Closed simulated position #{ticket} @ ${current_price:.2f} | PnL: ${pnl:.2f} ({hist_item['outcome']})")
        return hist_item

    def can_enter_trade(self, symbol: str, action: str, price: float, open_positions: List[Dict[str, Any]]) -> Tuple[bool, str]:
        """
        Institutional Risk & Anti-Chasing Gate:
        Enforces professional discipline:
          1. Maximum 1 active position per symbol (no duplicate stacking).
          2. Minimum 180s setup spacing cooldown.
          3. Post-TP Anti-Chasing Hysteresis (blocks buying the top after TP).
          4. Post-SL Anti-Revenge Lockout (15-min cooldown after stop-loss).
        """
        now = time.time()
        act_upper = "BUY" if "BUY" in action.upper() else "SELL"

        # 1. Check auto trade setting
        if not self.settings.get("auto_trade_enabled", True):
            return False, "Auto-trading is disabled in settings"

        # 2. Maximum 1 active position per symbol (strictly eliminates duplicate orders)
        symbol_pos = [p for p in open_positions if p.get("symbol") == symbol]
        if len(symbol_pos) >= 1:
            ticket = symbol_pos[0].get('ticket', 'N/A')
            return False, f"Position already active for {symbol} (Ticket #{ticket}). Max allowed: 1 per symbol."

        max_allowed = int(self.settings.get("max_open_positions", 2))
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
                    # Must retrace at least 5.0 points below the TP exit
                    pullback_dist = self.last_closed_price - price
                    if pullback_dist < 5.0:
                        return False, (
                            f"Post-TP Anti-Chasing Lockout: Price (${price:.2f}) has not retraced "
                            f"at least $5.00 below recent TP exit (${self.last_closed_price:.2f}). "
                            f"Pullback so far: ${pullback_dist:.2f}. Waiting for market retracement."
                        )
                elif act_upper == "SELL" and self.last_closed_action == "SELL":
                    # Must retrace at least 5.0 points above the TP exit
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

        return True, "PASSED"

    def get_positions(self, is_live_broker: bool, current_price: float, symbol: str = "XAUUSD") -> List[Dict[str, Any]]:
        """Returns live active positions from MT5 or the simulated tracker."""
        if is_live_broker and MT5_AVAILABLE and mt5 is not None:
            raw_pos = mt5.positions_get(symbol=symbol) or mt5.positions_get() or ()
            positions = []
            for p in raw_pos:
                positions.append({
                    "ticket": int(p.ticket),
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
            return positions

        # Simulation mode: update floating PnL
        updated = []
        to_close = []
        for p in self.simulated_positions:
            is_buy = p["type"] == "BUY"
            px = current_price if current_price > 0 else p["price_open"]
            p["price_current"] = round(px, 2)
            pnl = (px - p["price_open"]) * p["volume"] * 100.0 if is_buy else \
                  (p["price_open"] - px) * p["volume"] * 100.0
            p["profit"] = round(pnl, 2)

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

    def get_history(self, is_live_broker: bool) -> List[Dict[str, Any]]:
        """Returns completed trades merged from MT5 deals and local journal."""
        if is_live_broker and MT5_AVAILABLE and mt5 is not None:
            now_dt = datetime.now(timezone.utc)
            from_dt = now_dt - timedelta(days=7)
            deals = mt5.history_deals_get(from_dt, now_dt)
            if deals:
                live_deals = []
                for d in reversed(deals):
                    # Only entry OUT (closed deals) or deals with realized profit
                    if d.entry in (1, 2) or d.profit != 0:
                        live_deals.append({
                            "ticket": int(d.ticket),
                            "time": int(d.time),
                            "time_str": datetime.fromtimestamp(d.time, timezone.utc).strftime("%m-%d %H:%M"),
                            "symbol": d.symbol,
                            "type": "BUY" if d.type == 0 else "SELL",
                            "volume": round(d.volume, 2),
                            "price_open": round(d.price, 2),
                            "price_close": round(d.price, 2),
                            "profit": round(d.profit, 2),
                            "outcome": "WIN" if d.profit >= 0 else "LOSS",
                            "comment": str(d.comment)
                        })
                        if len(live_deals) >= 40:
                            break
                if live_deals:
                    return live_deals

        return self.history[:50]


trade_manager = TradeManager()
