"""
SparkX EA - Stop Loss (SL) Post-Mortem & Diagnostic Engine
Performs forensic quantitative root-cause analysis when a trade exits via Stop Loss.
Identifies which Smart Money Concepts (SMC) premise failed, formulates counterfactual
lessons, and produces calibrated targets for continuous online retraining.
"""

import time
import logging
from dataclasses import dataclass, field, asdict
from typing import Dict, Any, List, Optional

logger = logging.getLogger("SparkX.SLAnalyzer")


@dataclass
class SLPostMortem:
    ticket: int
    symbol: str
    direction: str              # "BUY" or "SELL"
    open_time: float
    close_time: float
    duration_sec: float
    entry_price: float
    exit_price: float
    sl_price: float
    tp_price: float
    pnl: float
    root_cause: str             # e.g. "LIQUIDITY_HUNT_SWEEP", "FVG_INVERSION_FAILURE", etc.
    primary_failure: str
    detailed_diagnosis: str
    rectification_rule: str
    counterfactual_choice: int  # 4 = HOLD
    counterfactual_score: float # e.g. 1.8 / 10.0
    counterfactual_noul: List[float] # corrected truth values
    compressed_state: str
    snapshot_features: Dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)


class SLDiagnosticEngine:
    """
    Forensic SMC diagnostic engine that attributes why an SL was triggered
    and derives the counterfactual ground-truth for model retraining.
    """

    ROOT_CAUSES = [
        "HTF_TREND_CONFLICT",
        "LIQUIDITY_HUNT_SWEEP",
        "FVG_INVERSION_FAILURE",
        "OB_MITIGATION_BLOWTHROUGH",
        "PREMIUM_DISCOUNT_VIOLATION",
        "DISPLACEMENT_EXHAUSTION",
        "SPREAD_VOLATILITY_EXPANSION"
    ]

    @staticmethod
    def diagnose(
        ticket: int,
        symbol: str,
        direction: str,
        open_time: float,
        close_time: float,
        entry_price: float,
        exit_price: float,
        sl_price: float,
        tp_price: float,
        pnl: float,
        compressed_state: str,
        features_at_entry: Dict[str, Any],
        features_at_exit: Optional[Dict[str, Any]] = None,
        min_price_during_trade: Optional[float] = None,
        max_price_during_trade: Optional[float] = None,
    ) -> SLPostMortem:
        """
        Evaluates the trade lifecycle against institutional Smart Money failure modes.
        """
        duration = max(1.0, close_time - open_time)
        f_entry = features_at_entry or {}
        f_exit = features_at_exit or {}

        # 1. Macro Trend Bias Checks
        h1_trend_entry = str(f_entry.get("h1_trend", "")).upper()
        h1_trend_exit = str(f_exit.get("h1_trend", h1_trend_entry)).upper()
        m15_struct_entry = str(f_entry.get("m15_struct", "")).upper()

        # 2. Zone and Dealing Range
        zone_entry = str(f_entry.get("price_zone", f_entry.get("zone", ""))).upper()
        fib_pct = float(f_entry.get("fib_pct", 50.0))

        # 3. Liquidity Levels
        asia_high = float(f_entry.get("asia_high", 0.0))
        asia_low = float(f_entry.get("asia_low", 0.0))
        ssl_swept = bool(f_entry.get("ssl_swept", False))
        bsl_swept = bool(f_entry.get("bsl_swept", False))

        # 4. Arrays
        fvg_active = bool(f_entry.get("fvg_active", False))
        ob_active = bool(f_entry.get("ob_active", False))
        displacement = bool(f_entry.get("displacement", False))
        spread = float(f_entry.get("spread", 1.5))

        root_cause = "UNKNOWN_LOSS"
        primary_failure = ""
        detailed_diagnosis = ""
        rectification_rule = ""

        # Check Failure Mode 1: HTF Trend Conflict (Trading against Higher Timeframe Flow)
        if direction == "BUY" and ("BEAR" in h1_trend_entry or "BEAR" in h1_trend_exit):
            root_cause = "HTF_TREND_CONFLICT"
            primary_failure = "H1 Macro Bearish Flow Overwhelmed Lower-Timeframe Long"
            detailed_diagnosis = (
                f"Long setup entered at ${entry_price:.2f} while H1 macro trend was {h1_trend_entry}. "
                f"Counter-trend retracement was rejected by higher-timeframe order flow, forcing price down to SL ${exit_price:.2f}."
            )
            rectification_rule = "Forbid BUY executions whenever H1 macro trend is BEARISH; require strict HTF alignment."
        elif direction == "SELL" and ("BULL" in h1_trend_entry or "BULL" in h1_trend_exit):
            root_cause = "HTF_TREND_CONFLICT"
            primary_failure = "H1 Macro Bullish Flow Overwhelmed Lower-Timeframe Short"
            detailed_diagnosis = (
                f"Short setup entered at ${entry_price:.2f} while H1 macro trend was {h1_trend_entry}. "
                f"Higher-timeframe institutional buying expanded price upward into SL ${exit_price:.2f}."
            )
            rectification_rule = "Forbid SELL executions whenever H1 macro trend is BULLISH; require strict HTF alignment."

        # Check Failure Mode 2: Premature Entry before Full Liquidity Hunt (Wicked out at Asian Extremes)
        elif direction == "BUY" and asia_low > 0 and (exit_price <= asia_low or (min_price_during_trade and min_price_during_trade <= asia_low)):
            root_cause = "LIQUIDITY_HUNT_SWEEP"
            primary_failure = "Premature Long Before Sell-Side Liquidity (SSL) Liquidation"
            detailed_diagnosis = (
                f"Trade entered at ${entry_price:.2f} before Asian Session Low (${asia_low:.2f}) was cleared. "
                f"Market dropped to ${exit_price:.2f} to hunt institutional resting stop orders at Asian Low before reversing."
            )
            rectification_rule = "Require clear wick penetration below Asian Low with displacement confirmation before taking Long setups in NY session."
        elif direction == "SELL" and asia_high > 0 and (exit_price >= asia_high or (max_price_during_trade and max_price_during_trade >= asia_high)):
            root_cause = "LIQUIDITY_HUNT_SWEEP"
            primary_failure = "Premature Short Before Buy-Side Liquidity (BSL) Liquidation"
            detailed_diagnosis = (
                f"Trade entered at ${entry_price:.2f} before Asian Session High (${asia_high:.2f}) was cleared. "
                f"Market expanded to ${exit_price:.2f} to hunt buy-stop liquidity at Asian High before reversing."
            )
            rectification_rule = "Require clear wick penetration above Asian High with displacement confirmation before taking Short setups in NY session."

        # Check Failure Mode 3: Dealing Range & Pricing Violation
        elif direction == "BUY" and ("PREMIUM" in zone_entry or fib_pct < 45.0):
            root_cause = "PREMIUM_DISCOUNT_VIOLATION"
            primary_failure = "Buying in Premium Dealing Range (Unfavorable Pricing)"
            detailed_diagnosis = (
                f"Long setup was initiated in the {zone_entry} zone (Fib {fib_pct:.1f}%). "
                f"Institutions sell in Premium rather than buy, resulting in immediate liquidation down to ${exit_price:.2f}."
            )
            rectification_rule = "Strictly gate BUY entries: must be below Equilibrium (Fib >= 50.0% in DISCOUNT)."
        elif direction == "SELL" and ("DISCOUNT" in zone_entry or fib_pct > 55.0):
            root_cause = "PREMIUM_DISCOUNT_VIOLATION"
            primary_failure = "Selling in Discount Dealing Range (Unfavorable Pricing)"
            detailed_diagnosis = (
                f"Short setup was initiated in the {zone_entry} zone (Fib {fib_pct:.1f}%). "
                f"Institutions accumulate in Discount rather than distribute, triggering upward liquidation to ${exit_price:.2f}."
            )
            rectification_rule = "Strictly gate SELL entries: must be above Equilibrium (Fib <= 50.0% in PREMIUM)."

        # Check Failure Mode 4: Fair Value Gap Inversion / Array Failure
        elif fvg_active:
            root_cause = "FVG_INVERSION_FAILURE"
            primary_failure = "M5 Fair Value Gap Inverted (Support Inverted to Resistance)"
            detailed_diagnosis = (
                f"M5 Fair Value Gap failed to hold as institutional support/resistance. "
                f"Heavy volume breached the array boundary, converting it into an inverted gap that accelerated price toward SL."
            )
            rectification_rule = "Require candle body close confirmation above FVG (or below for shorts) instead of blind touch limit orders."

        # Check Failure Mode 5: Order Block Invalidation
        elif ob_active:
            root_cause = "OB_MITIGATION_BLOWTHROUGH"
            primary_failure = "Order Block 50% Mean Threshold Violated"
            detailed_diagnosis = (
                f"Institutional Order Block absorption failed. Counter-party volume surpassed the block capacity, "
                f"breaking the 50% mitigation equilibrium and hitting SL at ${exit_price:.2f}."
            )
            rectification_rule = "Enforce OB threshold invalidation rule: close trade immediately if M5 body closes beyond OB midpoint."

        # Check Failure Mode 6: Lack of Institutional Displacement
        elif not displacement:
            root_cause = "DISPLACEMENT_EXHAUSTION"
            primary_failure = "Lack of Institutional Displacement / Low Energy Move"
            detailed_diagnosis = (
                f"Market moved into entry without strong candle displacement or volume expansion. "
                f"Retail volume was trapped without institutional sponsor support."
            )
            rectification_rule = "Do not take trades without verified displacement (at least 1.5x average body range with fresh imbalance)."

        # Fallback Failure Mode: Spread or Noise
        else:
            root_cause = "SPREAD_VOLATILITY_EXPANSION"
            primary_failure = "Market Volatility Noise / Spread Extension"
            detailed_diagnosis = (
                f"Trade was stopped out at ${exit_price:.2f} during session noise or spread widening ({spread:.1f} pts)."
            )
            rectification_rule = "Increase SL buffer distance by +0.3 ATR during high-volatility session overlaps."

        # Formulate Counterfactual Ground Truth for Online Retraining:
        # 1. The choice action MUST be penalized to HOLD (4)
        counterfactual_choice = 4
        # 2. Setup quality score is downgraded to 1.5 - 2.2 / 10.0 (failing institutional grade)
        counterfactual_score = round(max(1.0, min(2.5, 1.8 + (pnl / 100.0) * -0.5)), 2)
        # 3. Noul truth hypotheses corrected
        corrected_noul = [
            1.0 if (ssl_swept or bsl_swept) and root_cause != "LIQUIDITY_HUNT_SWEEP" else 0.0,
            0.0 if root_cause == "FVG_INVERSION_FAILURE" else (1.0 if fvg_active else 0.0),
            0.0 if root_cause in ("DISPLACEMENT_EXHAUSTION", "HTF_TREND_CONFLICT") else (1.0 if displacement else 0.0)
        ]

        post_mortem = SLPostMortem(
            ticket=ticket,
            symbol=symbol,
            direction=direction,
            open_time=open_time,
            close_time=close_time,
            duration_sec=duration,
            entry_price=entry_price,
            exit_price=exit_price,
            sl_price=sl_price,
            tp_price=tp_price,
            pnl=pnl,
            root_cause=root_cause,
            primary_failure=primary_failure,
            detailed_diagnosis=detailed_diagnosis,
            rectification_rule=rectification_rule,
            counterfactual_choice=counterfactual_choice,
            counterfactual_score=counterfactual_score,
            counterfactual_noul=corrected_noul,
            compressed_state=compressed_state,
            snapshot_features=f_entry
        )

        logger.warning(
            f"[SL POST-MORTEM] #{ticket} {direction} {symbol} (-${abs(pnl):.2f}) | "
            f"Root Cause: {root_cause} | Rectification: {rectification_rule}"
        )
        return post_mortem
