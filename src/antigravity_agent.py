"""
Antigravity Algorithmic Trading Agent & Event-Driven Engine
SparkX Terminal - High-Frequency SMC Ingestion, Compression, and Decision Loop.
Orchestrates MT5 multi-timeframe streams, sub-400 token state compression,
concurrent Laya GPU inference (~7ms-33ms), and deterministic trade execution.
"""

import os
import asyncio
import time
import logging
import math
from datetime import datetime, timezone, timedelta
from typing import List, Dict, Optional, Any, Tuple

from config.trading_config import SystemConfig, RiskConfig, LayaConfig, SMCConfig
from src.smc_engine import SMCEngine, Bar, SMCFeatures
from src.compressor import MarketStateCompressor
from src.laya_engine import LayaInferenceEngine, LayaDecision
from src.execution_gateway import MT5ExecutionGateway, ExecutionReceipt

# Optional MetaTrader 5 import
try:
    import MetaTrader5 as mt5
    MT5_AVAILABLE = True
except ImportError:
    MT5_AVAILABLE = False
    mt5 = None

# Optional Google Antigravity SDK import
try:
    from google.antigravity import Agent, LocalAgentConfig
    from google.antigravity.triggers import every, TriggerContext
    ANTIGRAVITY_AVAILABLE = True
except ImportError:
    ANTIGRAVITY_AVAILABLE = False
    TriggerContext = Any

# Setup high-resolution logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] [%(name)s] %(message)s'
)
logger = logging.getLogger("SparkX.AntigravityAgent")


class MT5DataIngestionService:
    """
    High-speed data ingestion fetching M5, M15, and H1 OHLCV bars + real-time ticks.
    Includes synthetic high-fidelity tick generator for offline / headless environments.
    """

    def __init__(self, symbols: List[str]):
        self.symbols = symbols
        self.is_live = False
        self._check_mt5_connection()

    def _check_mt5_connection(self):
        if not MT5_AVAILABLE:
            logger.info("MetaTrader5 package not detected. Active in Synthetic Market Stream mode.")
            return

        # Attempt 1: Standard IPC connection
        if mt5.initialize():
            self.is_live = True
            acc = mt5.account_info()
            server_name = acc.server if acc else "Default"
            logger.info(f"MT5 Ingestion Service connected to live broker terminal (Server: {server_name}).")
            return

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
                    self.is_live = True
                    acc = mt5.account_info()
                    server_name = acc.server if acc else "Default"
                    logger.info(f"MT5 Ingestion Service connected to live broker terminal at {cand} (Server: {server_name}).")
                    for s in self.symbols:
                        mt5.symbol_select(s, True)
                    return

        logger.warning("MT5 terminal unreachable. Running in High-Fidelity Simulation Stream.")

    def fetch_bars(self, symbol: str, timeframe_str: str, count: int = 60) -> List[Bar]:
        """Fetches OHLCV bars from MT5 or synthetic generator."""
        if self.is_live and MT5_AVAILABLE:
            mt5.symbol_select(symbol, True)
            tf_map = {
                "M5": mt5.TIMEFRAME_M5,
                "M15": mt5.TIMEFRAME_M15,
                "H1": mt5.TIMEFRAME_H1
            }
            tf = tf_map.get(timeframe_str, mt5.TIMEFRAME_M5)
            rates = mt5.copy_rates_from_pos(symbol, tf, 0, count)
            if rates is not None and len(rates) > 0:
                bars = []
                for r in rates:
                    dt = datetime.fromtimestamp(r['time'], tz=timezone.utc)
                    bars.append(Bar(
                        time=dt,
                        open=float(r['open']),
                        high=float(r['high']),
                        low=float(r['low']),
                        close=float(r['close']),
                        volume=float(r['tick_volume'])
                    ))
                return bars

        # Synthetic generator fallback: Realistic Asian sweep & FVG price progression
        return self._generate_synthetic_bars(symbol, timeframe_str, count)

    def _generate_synthetic_bars(self, symbol: str, tf_str: str, count: int) -> List[Bar]:
        """Generates realistic institutional SMC price action for validation and benchmarking."""
        now = datetime.now(timezone.utc)
        delta_min = 5 if tf_str == "M5" else (15 if tf_str == "M15" else 60)
        
        base_price = 2360.0 if "XAU" in symbol else 64200.0
        step_pts = 0.8 if "XAU" in symbol else 35.0
        
        bars = []
        for i in range(count):
            bar_time = now - timedelta(minutes=(count - i) * delta_min)
            # Create an Asian session consolidation (00:00 - 08:00) then a sweep at 09:00
            hour = bar_time.hour
            if hour < 8:
                # Asian range
                open_p = base_price + (math.sin(i * 0.3) * step_pts)
                high_p = open_p + (step_pts * 0.6)
                low_p = open_p - (step_pts * 0.6)
                close_p = open_p + (math.cos(i * 0.3) * step_pts * 0.4)
            elif 8 <= hour < 10:
                # London Judas swing: Sweep below Asian low
                open_p = base_price - (step_pts * 1.5)
                low_p = open_p - (step_pts * 2.2)  # Wick sweeps liquidity
                high_p = open_p + (step_pts * 0.3)
                close_p = open_p + (step_pts * 0.8) # Sharp rejection
            else:
                # NY displacement up leaving Bullish FVG & MSS
                open_p = base_price + (i * 0.2 * step_pts)
                high_p = open_p + (step_pts * 1.8)
                low_p = open_p - (step_pts * 0.2)
                close_p = high_p - (step_pts * 0.1)

            bars.append(Bar(
                time=bar_time,
                open=round(open_p, 2),
                high=round(high_p, 2),
                low=round(low_p, 2),
                close=round(close_p, 2),
                volume=1200.0 + (i * 15.0)
            ))
        return bars

    def fetch_live_tick(self, symbol: str) -> Tuple[float, float, float]:
        """Returns (bid, ask, spread_in_points)."""
        if self.is_live and MT5_AVAILABLE:
            mt5.symbol_select(symbol, True)
            tick = mt5.symbol_info_tick(symbol)
            if tick and tick.bid > 0:
                spread = (tick.ask - tick.bid) / (0.01 if "XAU" in symbol else 1.0)
                return tick.bid, tick.ask, spread
        
        # Synthetic tick fallback
        base = 2364.50 if "XAU" in symbol else 64450.0
        spread = 1.2 if "XAU" in symbol else 5.0
        return base, base + (spread * 0.01), spread


class SparkXTradingAgent:
    """
    Antigravity-compliant Reactive Algorithmic Trading Decision Agent.
    Coordinates Ingestion -> SMC Computation -> State Compression -> Laya GPU Inference -> Execution.
    """

    def __init__(self, config: Optional[SystemConfig] = None):
        self.config = config or SystemConfig()
        self.ingestion = MT5DataIngestionService(self.config.symbols)
        self.laya = LayaInferenceEngine(
            onnx_model_path=self.config.laya.onnx_model_path,
            temperature=1.35,
            device_id=self.config.laya.gpu_device_id
        )
        self.gateway = MT5ExecutionGateway(self.config.risk)
        self.smc_engines = {
            sym: SMCEngine(sym, point_value=0.01 if "XAU" in sym else 1.0)
            for sym in self.config.symbols
        }
        self.is_running = False

    async def process_symbol_event(self, symbol: str) -> Optional[ExecutionReceipt]:
        """
        Single-turn, ultra-low-latency pipeline pass for a given trading symbol.
        Monitors latency telemetry across all sub-components.
        """
        t_total_start = time.perf_counter()

        # Step 1: Ingest multi-timeframe market data (M5, M15, H1)
        t_ingest_start = time.perf_counter()
        bars_m5 = self.ingestion.fetch_bars(symbol, "M5", count=60)
        bars_m15 = self.ingestion.fetch_bars(symbol, "M15", count=40)
        bars_h1 = self.ingestion.fetch_bars(symbol, "H1", count=40)
        bid, ask, spread = self.ingestion.fetch_live_tick(symbol)
        t_ingest_ms = (time.perf_counter() - t_ingest_start) * 1000.0

        # Step 2: Compute SMC features & Price Action vectors
        t_smc_start = time.perf_counter()
        smc = self.smc_engines[symbol]
        features = smc.extract_features(bars_m5, bars_m15, bars_h1, spread_points=spread)
        t_smc_ms = (time.perf_counter() - t_smc_start) * 1000.0

        # Step 3: Compress market state into dense vector (< 400 tokens)
        t_comp_start = time.perf_counter()
        compressed_state = MarketStateCompressor.compress(features)
        token_stats = MarketStateCompressor.get_summary_stats(compressed_state)
        t_comp_ms = (time.perf_counter() - t_comp_start) * 1000.0

        logger.info(
            f"[{symbol}] Compressed State: {token_stats['estimated_tokens']} est tokens "
            f"({token_stats['char_count']} chars) | Limit: {self.config.laya.max_compressed_state_tokens}"
        )

        # Step 4: Laya GPU Inference (Choice, Score, and Noul evaluated concurrently)
        decision: LayaDecision = await self.laya.infer_confluent_decision(
            compressed_market_state=compressed_state
        )

        logger.info(
            f"[{symbol}] Laya Output -> Action: {decision.choice.action} ({decision.choice.probability:.1%}) | "
            f"Score: {decision.score.grade}/10 | Inf Latency: {decision.inference_latency_ms:.2f}ms "
            f"[Backend: {self.laya.execution_provider}]"
        )

        # Step 5: Deterministic Execution Gateway
        t_exec_start = time.perf_counter()
        receipt = self.gateway.route_execution(
            decision=decision,
            features=features,
            account_equity=100000.0
        )
        t_exec_ms = (time.perf_counter() - t_exec_start) * 1000.0

        t_total_ms = (time.perf_counter() - t_total_start) * 1000.0

        # High-precision latency telemetry report
        logger.info(
            f"[{symbol}] Telemetry breakdown | Ingest: {t_ingest_ms:.1f}ms | SMC+Comp: {t_smc_ms+t_comp_ms:.1f}ms | "
            f"Laya GPU: {decision.inference_latency_ms:.1f}ms | Gate+Exec: {t_exec_ms:.1f}ms | "
            f"TOTAL LATENCY: {t_total_ms:.2f}ms (Target: <40.0ms)"
        )

        if receipt.executed:
            logger.info(
                f"[EXECUTION TRIGGERED] {receipt.action} {receipt.volume} lots @ {receipt.price} "
                f"SL:{receipt.sl} TP:{receipt.tp} (Ticket #{receipt.order_id})"
            )
        else:
            logger.debug(f"[FILTERED] {receipt.status_message}")

        return receipt

    async def run_antigravity_loop(self):
        """
        Primary continuous event loop pulling live ticks and executing the pipeline.
        Designed to integrate with Antigravity agent triggers or run as a standalone daemon.
        """
        self.is_running = True
        logger.info("SparkX Terminal Antigravity Event Loop initiated.")
        logger.info(f"Target Assets: {self.config.symbols} | Poll interval: {self.config.poll_interval_sec}s")

        try:
            while self.is_running:
                for symbol in self.config.symbols:
                    await self.process_symbol_event(symbol)
                await asyncio.sleep(self.config.poll_interval_sec)
        except asyncio.CancelledError:
            logger.info("Antigravity Event Loop shutdown requested.")
        finally:
            self.is_running = False
            logger.info("SparkX Decision Agent safely terminated.")


# =============================================================================
# Antigravity Framework Trigger Integration
# =============================================================================

async def antigravity_market_trigger(ctx: TriggerContext):
    """
    Antigravity SDK periodic trigger integration hook.
    Can be registered into LocalAgentConfig / LiteRTAgentConfig triggers.
    """
    agent = SparkXTradingAgent()
    for symbol in ["XAUUSD"]:
        receipt = await agent.process_symbol_event(symbol)
        if receipt and receipt.executed:
            if hasattr(ctx, "send"):
                await ctx.send(
                    f"ORDER_EXECUTED: {receipt.action} {receipt.volume} {receipt.symbol} @ {receipt.price}"
                )


if __name__ == "__main__":
    import math
    agent = SparkXTradingAgent()
    asyncio.run(agent.process_symbol_event("XAUUSD"))
