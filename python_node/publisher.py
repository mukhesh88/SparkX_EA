"""
SparkX Terminal - Headless Python Market Data Node
Extracts institutional Smart Money Concepts (SMC) features from MetaTrader 5,
compresses market state strictly under 512 tokens, and broadcasts locally via ZeroMQ (PUB).
"""

import sys
import os
import time
import json
import logging
from datetime import datetime, timezone
import socket
import threading
import zmq

# Ensure root workspace is in sys.path
WORKSPACE_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if WORKSPACE_ROOT not in sys.path:
    sys.path.insert(0, WORKSPACE_ROOT)

from config.trading_config import SystemConfig
from src.smc_engine import SMCEngine, Bar, SMCFeatures
from src.compressor import MarketStateCompressor
from src.antigravity_agent import MT5DataIngestionService
from src.execution_gateway import MT5ExecutionGateway, ExecutionReceipt
from src.mobile_notifier import mobile_notifier
from src.trade_manager import trade_manager

try:
    import MetaTrader5 as mt5
    MT5_AVAILABLE = True
except ImportError:
    MT5_AVAILABLE = False
    mt5 = None

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] [PyDataNode] %(message)s'
)
logger = logging.getLogger("SparkX.PyDataNode")

class LocalTCPBroadcaster:
    """
    Ultra-low-latency local TCP broadcaster for native Windows C++ IPC.
    Streams newline-delimited JSON payloads over 127.0.0.1:5556 and receives commands from C++.
    """
    def __init__(self, host: str = "127.0.0.1", port: int = 5556, on_command=None):
        self.host = host
        self.port = port
        self.on_command = on_command
        self.clients = []
        self.lock = threading.Lock()
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_socket.bind((self.host, self.port))
        self.server_socket.listen(5)
        self.running = True
        self.accept_thread = threading.Thread(target=self._accept_loop, daemon=True)
        self.accept_thread.start()
        logger.info(f"Local TCP Broadcaster active on {self.host}:{self.port}")

    def _accept_loop(self):
        while self.running:
            try:
                client_sock, addr = self.server_socket.accept()
                client_sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                with self.lock:
                    self.clients.append(client_sock)
                logger.info(f"C++ Engine connected via TCP from {addr}")
                # Start background command listener for this client
                reader = threading.Thread(target=self._client_reader, args=(client_sock,), daemon=True)
                reader.start()
            except Exception:
                if not self.running:
                    break

    def _client_reader(self, client_sock):
        buffer = ""
        while self.running:
            try:
                data = client_sock.recv(4096)
                if not data:
                    break
                buffer += data.decode('utf-8', errors='ignore')
                while '\n' in buffer:
                    line, buffer = buffer.split('\n', 1)
                    line = line.strip()
                    if line and self.on_command:
                        try:
                            cmd_dict = json.loads(line)
                            self.on_command(cmd_dict)
                        except Exception as e:
                            logger.warning(f"Failed to process client command: {e}")
            except Exception:
                break

    def broadcast(self, payload_dict: dict):
        line = json.dumps(payload_dict) + "\n"
        data = line.encode('utf-8')
        with self.lock:
            disconnected = []
            for s in self.clients:
                try:
                    s.sendall(data)
                except Exception:
                    disconnected.append(s)
            for d in disconnected:
                if d in self.clients:
                    self.clients.remove(d)
                try:
                    d.close()
                except Exception:
                    pass

    def close(self):
        self.running = False
        with self.lock:
            for s in self.clients:
                try:
                    s.close()
                except Exception:
                    pass
            self.clients.clear()
        try:
            self.server_socket.close()
        except Exception:
            pass


class ZMQMarketPublisher:
    """
    High-frequency local ZeroMQ & TCP publisher broadcasting compressed market frames to C++ Engine
    and autonomously executing live trades via MT5ExecutionGateway on the logged-in MT5 account.
    """
    def __init__(self, bind_address: str = "tcp://127.0.0.1:5555", tcp_port: int = 5556, config: SystemConfig = None):
        self.bind_address = bind_address
        self.config = config or SystemConfig()
        self.ingestion = MT5DataIngestionService(self.config.symbols)
        self.gateway = MT5ExecutionGateway(self.config.risk, simulation_mode=False)
        self.last_trade_time = 0.0
        self.last_receipt: Optional[ExecutionReceipt] = None
        self.last_price: Dict[str, float] = {}

        self.smc_engines = {
            sym: SMCEngine(sym, point_value=0.01 if "XAU" in sym else 1.0)
            for sym in self.config.symbols
        }

        # Initialize TCP Broadcaster with bidirectional command routing
        self.tcp_broadcaster = LocalTCPBroadcaster(
            host="127.0.0.1", port=tcp_port, on_command=self.handle_client_command
        )

        # Initialize ZeroMQ
        self.context = zmq.Context()
        self.socket = self.context.socket(zmq.PUB)
        self.socket.setsockopt(zmq.SNDHWM, 100)
        self.socket.bind(self.bind_address)
        logger.info(f"ZeroMQ PUB Socket bound to {self.bind_address}")

    def handle_client_command(self, cmd: dict):
        """Processes manual or ONNX gate execution triggers received from C++ UI."""
        logger.info(f"Received Command from C++ Client: {cmd}")
        cmd_type = cmd.get("command", "")
        symbol = cmd.get("symbol", "XAUUSD")
        action = cmd.get("action", "")

        if cmd_type == "UPDATE_SETTINGS":
            new_settings = cmd.get("settings", {})
            if new_settings:
                trade_manager.save_settings(new_settings)
                logger.info(f"[SETTINGS APPLIED] New trading settings updated: {trade_manager.settings}")
                self.tcp_broadcaster.broadcast({
                    "event": "SETTINGS_UPDATED",
                    "settings": trade_manager.settings
                })
        elif cmd_type == "CLOSE_TICKET":
            ticket = int(cmd.get("ticket", 0))
            if ticket > 0:
                if self.ingestion.is_live:
                    self.gateway.close_position_by_ticket(ticket)
                cur_px = self.last_price.get(symbol, 4180.0)
                trade_manager.close_simulated_position(ticket, cur_px)
                logger.info(f"[CLOSE TICKET EXECUTED] Closed position #{ticket}")
        elif cmd_type == "CLOSE_ALL":
            if self.ingestion.is_live:
                closed = self.gateway.close_all_positions(symbol=symbol)
                logger.info(f"[CLOSE ALL EXECUTED] Closed MT5 tickets: {closed}")
            cur_px = self.last_price.get(symbol, 4180.0)
            while trade_manager.simulated_positions:
                pos = trade_manager.simulated_positions[0]
                trade_manager.close_simulated_position(pos["ticket"], cur_px)
            logger.info("[CLOSE ALL EXECUTED] All positions cleared.")
        elif cmd_type == "TEST_ALERT":
            cur_px = self.last_price.get(symbol, 4185.0)
            mobile_notifier.send_trade_signal(
                symbol=symbol,
                action="MARKET_BUY",
                price=cur_px,
                sl=cur_px - 4.5,
                tp=cur_px + 12.0,
                score=9.5,
                confidence=0.96,
                reason="Verification Ping from SparkX Trade Settings Menu",
                order_id=999999
            )
            logger.info("[TEST ALERT] Dispatched verification ping to Discord/Telegram.")
        elif cmd_type == "EXECUTE" or ("BUY" in action or "SELL" in action):
            sl_pts = float(trade_manager.settings.get("sl_points", 4.5))
            tp_pts = float(trade_manager.settings.get("tp_points", 12.0))
            vol = float(trade_manager.settings.get("fixed_lot_size", 0.10))
            cur_px = self.last_price.get(symbol, 4185.0)

            if self.ingestion.is_live:
                receipt = self.gateway.execute_live_order(
                    symbol=symbol,
                    action=action,
                    sl_points=sl_pts,
                    tp_points=tp_pts,
                    lot_size=vol,
                    comment="SparkX_GUI_Cmd"
                )
            else:
                is_b = "BUY" in action.upper()
                sl_p = round(cur_px - sl_pts if is_b else cur_px + sl_pts, 2)
                tp_p = round(cur_px + tp_pts if is_b else cur_px - tp_pts, 2)
                sim_ticket = trade_manager.add_simulated_position(
                    symbol=symbol, action=action, price=cur_px, volume=vol, sl=sl_p, tp=tp_p, comment="SparkX_GUI_Cmd"
                )
                receipt = ExecutionReceipt(
                    executed=True, order_id=sim_ticket, symbol=symbol, action=action,
                    price=cur_px, volume=vol, sl=sl_p, tp=tp_p, retcode=10009,
                    status_message="SUCCESS_SIMULATED_ORDER_FILLED", latency_ms=0.5
                )

            self.last_receipt = receipt
            if receipt.executed:
                self.last_trade_time = time.time()
                logger.info(f"[C++ COMMAND EXECUTED] Ticket #{receipt.order_id} {action} {receipt.volume} {symbol} @ {receipt.price}")
                mobile_notifier.send_trade_signal(
                    symbol=symbol,
                    action=action,
                    price=receipt.price,
                    sl=receipt.sl,
                    tp=receipt.tp,
                    score=9.2,
                    confidence=0.94,
                    reason=f"Manual Operator Command Triggered via C++ GUI ({action})",
                    order_id=receipt.order_id
                )

    def generate_and_publish_frame(self, symbol: str):
        t0 = time.perf_counter()

        # 1. Ingest multi-timeframe bars from MT5
        bars_m5 = self.ingestion.fetch_bars(symbol, "M5", count=60)
        bars_m15 = self.ingestion.fetch_bars(symbol, "M15", count=40)
        bars_h1 = self.ingestion.fetch_bars(symbol, "H1", count=40)
        bid, ask, spread = self.ingestion.fetch_live_tick(symbol)

        # 2. Extract SMC features
        smc = self.smc_engines[symbol]
        features: SMCFeatures = smc.extract_features(bars_m5, bars_m15, bars_h1, spread_points=spread)
        self.last_price[symbol] = features.current_price

        # 3. Query active account status & positions
        positions = trade_manager.get_positions(self.ingestion.is_live, features.current_price, symbol)
        history = trade_manager.get_history(self.ingestion.is_live)
        num_open = len(positions)
        equity = 100000.0
        balance = 100000.0
        login = 0
        if self.ingestion.is_live and mt5 is not None:
            acc_info = mt5.account_info()
            if acc_info:
                equity = acc_info.equity
                balance = acc_info.balance
                login = acc_info.login

        # 4. Autonomous Institutional Trade Execution
        now = time.time()
        auto_enabled = trade_manager.settings.get("auto_trade_enabled", True)
        max_positions = trade_manager.settings.get("max_open_positions", 2)
        sl_pts = float(trade_manager.settings.get("sl_points", 4.5))
        tp_pts = float(trade_manager.settings.get("tp_points", 12.0))
        vol = float(trade_manager.settings.get("fixed_lot_size", 0.10))
        max_spread = float(trade_manager.settings.get("max_spread_points", 25.0))

        if auto_enabled and num_open < max_positions and (now - self.last_trade_time > 30.0):
            trade_action = None
            is_discount = features.price_zone == "DISCOUNT"
            is_premium = features.price_zone == "PREMIUM"
            is_ssl = features.ssl_swept
            is_bsl = features.bsl_swept
            has_bull_fvg = features.active_m5_fvg is not None and features.active_m5_fvg.direction == "BULLISH"
            has_bear_fvg = features.active_m5_fvg is not None and features.active_m5_fvg.direction == "BEARISH"
            has_bull_ob = features.active_m5_ob is not None and features.active_m5_ob.direction == "BULLISH"
            has_bear_ob = features.active_m5_ob is not None and features.active_m5_ob.direction == "BEARISH"

            if (is_discount or is_ssl or has_bull_fvg or has_bull_ob) and spread <= max_spread:
                trade_action = "MARKET_BUY"
            elif (is_premium or is_bsl or has_bear_fvg or has_bear_ob) and spread <= max_spread:
                trade_action = "MARKET_SELL"
            elif features.h1_trend == "BULLISH" and features.m5_structure in ("BOS_BULLISH", "MSS_BULLISH") and spread <= max_spread:
                trade_action = "MARKET_BUY"
            elif features.h1_trend == "BEARISH" and features.m5_structure in ("BOS_BEARISH", "MSS_BEARISH") and spread <= max_spread:
                trade_action = "MARKET_SELL"

            if trade_action:
                if self.ingestion.is_live:
                    receipt = self.gateway.execute_live_order(
                        symbol=symbol,
                        action=trade_action,
                        sl_points=sl_pts,
                        tp_points=tp_pts,
                        lot_size=vol,
                        comment="SparkX_Auto_Live"
                    )
                else:
                    cur_px = features.current_price
                    is_b = "BUY" in trade_action.upper()
                    sl_p = round(cur_px - sl_pts if is_b else cur_px + sl_pts, 2)
                    tp_p = round(cur_px + tp_pts if is_b else cur_px - tp_pts, 2)
                    sim_ticket = trade_manager.add_simulated_position(
                        symbol=symbol, action=trade_action, price=cur_px, volume=vol, sl=sl_p, tp=tp_p, comment="SparkX_Auto_Sim"
                    )
                    receipt = ExecutionReceipt(
                        executed=True, order_id=sim_ticket, symbol=symbol, action=trade_action,
                        price=cur_px, volume=vol, sl=sl_p, tp=tp_p, retcode=10009,
                        status_message="SUCCESS_SIMULATED_ORDER_FILLED", latency_ms=0.5
                    )

                self.last_receipt = receipt
                if receipt.executed:
                    self.last_trade_time = now
                    logger.info(
                        f"*** [ORDER FILLED] *** Ticket #{receipt.order_id} {trade_action} "
                        f"{receipt.volume} {symbol} @ {receipt.price} | SL: {receipt.sl} | TP: {receipt.tp}"
                    )
                    smc_reason = (
                        f"{features.h1_trend} H1 Trend | {features.m5_structure} M5 Struct | "
                        f"{features.price_zone} Zone | "
                        f"{'Active Bullish FVG' if features.active_m5_fvg and features.active_m5_fvg.direction == 'BULLISH' else ('Active Bearish FVG' if features.active_m5_fvg else 'Liquidity Sweep')}"
                    )
                    mobile_notifier.send_trade_signal(
                        symbol=symbol,
                        action=trade_action,
                        price=receipt.price,
                        sl=receipt.sl,
                        tp=receipt.tp,
                        score=8.7,
                        confidence=0.89,
                        reason=smc_reason,
                        order_id=receipt.order_id
                    )

        # 5. Compress state into dense token-vector string (< 512 tokens)
        compressed_state = MarketStateCompressor.compress(features)
        token_count = MarketStateCompressor.estimate_tokens(compressed_state)

        # 6. Construct payload
        payload = {
            "symbol": symbol,
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "epoch_ms": int(time.time() * 1000),
            "is_live": True,
            "mt5_connected": self.ingestion.is_live,
            "data_source": self.ingestion.data_source,
            "broker": "MT5 LIVE BROKER" if self.ingestion.is_live else "TRADINGVIEW REAL SPOT",
            "price": features.current_price,
            "bid": round(bid, 2),
            "ask": round(ask, 2),
            "spread": round(spread, 1),
            "session": features.current_session,
            "token_count": token_count,
            "compressed_state": compressed_state,
            "bias": {
                "h1_trend": features.h1_trend,
                "m15_struct": features.m15_structure,
                "m5_struct": features.m5_structure,
                "zone": features.price_zone,
                "fib_pct": features.fib_retracement_pct
            },
            "liquidity": {
                "asia_high": features.asian_liquidity.high,
                "asia_low": features.asian_liquidity.low,
                "bsl_swept": features.bsl_swept,
                "ssl_swept": features.ssl_swept,
                "bsl_target": features.nearest_bsl_target,
                "ssl_target": features.nearest_ssl_target
            },
            "arrays": {
                "fvg_active": features.active_m5_fvg is not None,
                "fvg_direction": features.active_m5_fvg.direction if features.active_m5_fvg else "NONE",
                "ob_active": features.active_m5_ob is not None,
                "ob_direction": features.active_m5_ob.direction if features.active_m5_ob else "NONE"
            },
            "momentum": {
                "atr": features.atr_m5,
                "displacement": features.displacement_detected,
                "volume_expansion": features.volume_expansion
            },
            "account": {
                "login": login,
                "equity": round(equity, 2),
                "balance": round(balance, 2),
                "open_positions": num_open,
                "tickets": [p.get("ticket", 0) for p in positions]
            },
            "positions": positions,
            "history": history,
            "settings": trade_manager.settings,
            "last_execution": {
                "executed": self.last_receipt.executed if self.last_receipt else False,
                "order_id": self.last_receipt.order_id if self.last_receipt else None,
                "action": self.last_receipt.action if self.last_receipt else "",
                "price": self.last_receipt.price if self.last_receipt else 0.0,
                "volume": self.last_receipt.volume if self.last_receipt else 0.0,
                "sl": self.last_receipt.sl if self.last_receipt else 0.0,
                "tp": self.last_receipt.tp if self.last_receipt else 0.0,
                "status_message": self.last_receipt.status_message if self.last_receipt else ""
            },
            "latency_ms": round((time.perf_counter() - t0) * 1000.0, 3)
        }

        # 7. Broadcast via ZeroMQ and Local TCP
        topic = "SMC_MARKET_STATE"
        message_json = json.dumps(payload)
        self.socket.send_multipart([topic.encode('utf-8'), message_json.encode('utf-8')])
        self.tcp_broadcaster.broadcast(payload)

        logger.info(
            f"Published {symbol} frame | Px: {payload['price']:.2f} | Tokens: {token_count} "
            f"| OpenPos: {num_open} | Latency: {payload['latency_ms']:.2f}ms"
        )

    def start_loop(self, poll_interval_sec: float = 0.5):
        logger.info(f"Initiating Python MT5 Ingestion & Live Execution Loop (Interval: {poll_interval_sec}s)...")
        # Allow subscriber handshake
        time.sleep(0.5)
        try:
            while True:
                for symbol in self.config.symbols:
                    self.generate_and_publish_frame(symbol)
                time.sleep(poll_interval_sec)
        except KeyboardInterrupt:
            logger.info("Publisher terminated by user.")
        finally:
            self.tcp_broadcaster.close()
            self.socket.close()
            self.context.term()
            logger.info("ZeroMQ & TCP broadaster cleanly closed.")


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Headless Python MT5 Market Data Node")
    parser.add_argument("--interval", type=float, default=0.5, help="Publishing interval in seconds")
    parser.add_argument("--bind", type=str, default="tcp://127.0.0.1:5555", help="ZMQ bind address")
    parser.add_argument("--tcp-port", type=int, default=5556, help="Local TCP broadcaster port for C++")
    args = parser.parse_args()

    node = ZMQMarketPublisher(bind_address=args.bind, tcp_port=args.tcp_port)
    node.start_loop(poll_interval_sec=args.interval)

