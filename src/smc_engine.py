"""
Smart Money Concepts (SMC) & Price Action Feature Engine
Computes session liquidity, fair value gaps (FVG), order blocks (OB),
market structure shifts (MSS), and premium/discount pricing.
"""

from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import List, Dict, Optional, Tuple

@dataclass
class Bar:
    """Standardized OHLCV Bar."""
    time: datetime
    open: float
    high: float
    low: float
    close: float
    volume: float

@dataclass
class FVG:
    """Fair Value Gap structure."""
    direction: str          # 'BULLISH' or 'BEARISH'
    top: float              # Upper boundary of gap
    bottom: float           # Lower boundary of gap
    consequent_encroachment: float  # 50% midpoint
    bar_index: int
    time: datetime
    is_mitigated: bool = False
    mitigated_time: Optional[datetime] = None

@dataclass
class OrderBlock:
    """Order Block structure."""
    direction: str          # 'BULLISH' or 'BEARISH'
    high: float
    low: float
    open: float
    close: float
    bar_index: int
    time: datetime
    is_mitigated: bool = False
    volume_ratio: float = 1.0

@dataclass
class SwingPoint:
    """Swing High or Swing Low fractal."""
    type: str               # 'HIGH' or 'LOW'
    price: float
    bar_index: int
    time: datetime
    broken: bool = False

@dataclass
class SessionLiquidity:
    """Session High/Low and Sweep tracking."""
    session_name: str
    high: float
    low: float
    high_swept: bool = False
    low_swept: bool = False
    high_sweep_pips: float = 0.0
    low_sweep_pips: float = 0.0

@dataclass
class SMCFeatures:
    """Aggregated SMC features ready for compression and inference."""
    symbol: str
    timestamp: datetime
    current_price: float
    spread_points: float
    current_session: str
    
    # Session sweeps
    asian_liquidity: SessionLiquidity
    bsl_swept: bool
    ssl_swept: bool
    
    # Trends & Structure
    h1_trend: str           # 'BULLISH', 'BEARISH', 'RANGE'
    m15_structure: str      # 'BULLISH_BOS', 'BEARISH_BOS', 'MSS_BULL', 'MSS_BEAR', 'NEUTRAL'
    m5_structure: str
    
    # Premium / Discount
    dealing_range_high: float
    dealing_range_low: float
    equilibrium_price: float
    price_zone: str         # 'DISCOUNT', 'PREMIUM', 'EQUILIBRIUM'
    fib_retracement_pct: float
    
    # SMC Arrays
    active_m5_fvg: Optional[FVG]
    active_m5_ob: Optional[OrderBlock]
    nearest_bsl_target: float
    nearest_ssl_target: float
    
    # Momentum & Displacement
    atr_m5: float
    displacement_detected: bool
    volume_expansion: bool


class SMCEngine:
    """
    Computes real-time Smart Money Concepts (SMC) metrics across multiple timeframes.
    Designed for sub-millisecond execution overhead.
    """
    def __init__(self, symbol: str, point_value: float = 0.01):
        self.symbol = symbol
        self.point_value = point_value

    def get_current_session(self, dt_utc: datetime) -> str:
        """Determines active market session from UTC time."""
        hour = dt_utc.hour + dt_utc.minute / 60.0
        if 0.0 <= hour < 8.0:
            return "ASIAN"
        elif 8.0 <= hour < 13.0:
            return "LONDON"
        elif 13.0 <= hour < 16.5:
            return "NY_OVERLAP"
        elif 16.5 <= hour < 21.0:
            return "NY_AFTERNOON"
        else:
            return "PACIFIC_OFF_HOURS"

    def compute_asian_session_liquidity(self, bars: List[Bar]) -> SessionLiquidity:
        """
        Calculates Asian session (00:00-08:00 UTC) High & Low and checks for current day sweeps.
        """
        if not bars:
            return SessionLiquidity("ASIAN", 0.0, 0.0)

        latest_time = bars[-1].time
        latest_date = latest_time.date()
        current_close = bars[-1].close
        current_high = bars[-1].high
        current_low = bars[-1].low

        asian_bars = [
            b for b in bars 
            if b.time.date() == latest_date and 0 <= b.time.hour < 8
        ]

        if not asian_bars:
            # Fallback to previous day's Asian session
            asian_bars = [
                b for b in bars 
                if 0 <= b.time.hour < 8
            ][-16:]  # Up to 8 hours of M30 / 16 bars

        if not asian_bars:
            return SessionLiquidity("ASIAN", current_high, current_low)

        asia_high = max(b.high for b in asian_bars)
        asia_low = min(b.low for b in asian_bars)

        # Check post-Asian bars for sweeps (wick through high/low followed by reversal)
        post_asian_bars = [
            b for b in bars 
            if (b.time.date() == latest_date and b.time.hour >= 8) or b.time > asian_bars[-1].time
        ]

        high_swept = False
        low_swept = False
        high_sweep_pips = 0.0
        low_sweep_pips = 0.0

        for b in post_asian_bars:
            if b.high > asia_high and b.close < asia_high:
                high_swept = True
                high_sweep_pips = max(high_sweep_pips, (b.high - asia_high) / self.point_value)
            if b.low < asia_low and b.close > asia_low:
                low_swept = True
                low_sweep_pips = max(low_sweep_pips, (asia_low - b.low) / self.point_value)

        # Check latest live bar directly
        if current_high > asia_high and current_close <= asia_high:
            high_swept = True
            high_sweep_pips = max(high_sweep_pips, (current_high - asia_high) / self.point_value)
        if current_low < asia_low and current_close >= asia_low:
            low_swept = True
            low_sweep_pips = max(low_sweep_pips, (asia_low - current_low) / self.point_value)

        return SessionLiquidity(
            session_name="ASIAN",
            high=asia_high,
            low=asia_low,
            high_swept=high_swept,
            low_swept=low_swept,
            high_sweep_pips=round(high_sweep_pips, 1),
            low_sweep_pips=round(low_sweep_pips, 1)
        )

    def detect_fvgs(self, bars: List[Bar], min_gap_points: float = 0.5) -> List[FVG]:
        """
        Detects 3-candle Fair Value Gaps (FVG) and evaluates active mitigation state.
        Bullish FVG: Bar[i-2].high < Bar[i].low
        Bearish FVG: Bar[i-2].low > Bar[i].high
        """
        fvgs: List[FVG] = []
        n = len(bars)
        if n < 3:
            return fvgs

        for i in range(2, n):
            b_prev = bars[i - 2]
            b_mid = bars[i - 1]
            b_curr = bars[i]

            # Bullish FVG check
            if b_curr.low > b_prev.high + min_gap_points:
                top = b_curr.low
                bottom = b_prev.high
                ce = bottom + (top - bottom) * 0.5
                fvg = FVG(
                    direction="BULLISH",
                    top=top,
                    bottom=bottom,
                    consequent_encroachment=ce,
                    bar_index=i - 1,
                    time=b_mid.time
                )
                # Check mitigation in subsequent bars
                for j in range(i + 1, n):
                    if bars[j].low <= top:  # Tapped into gap
                        if bars[j].close < bottom:  # Invalidated/Filled
                            fvg.is_mitigated = True
                            fvg.mitigated_time = bars[j].time
                            break
                        else:
                            # Partially tapped but still active support
                            pass
                fvgs.append(fvg)

            # Bearish FVG check
            elif b_curr.high < b_prev.low - min_gap_points:
                top = b_prev.low
                bottom = b_curr.high
                ce = bottom + (top - bottom) * 0.5
                fvg = FVG(
                    direction="BEARISH",
                    top=top,
                    bottom=bottom,
                    consequent_encroachment=ce,
                    bar_index=i - 1,
                    time=b_mid.time
                )
                # Check mitigation in subsequent bars
                for j in range(i + 1, n):
                    if bars[j].high >= bottom:
                        if bars[j].close > top:
                            fvg.is_mitigated = True
                            fvg.mitigated_time = bars[j].time
                            break
                fvgs.append(fvg)

        return fvgs

    def detect_order_blocks(self, bars: List[Bar], atr: float) -> List[OrderBlock]:
        """
        Identifies institutional Order Blocks (OB).
        Bullish OB: Last down-close candle preceding strong upward displacement breaking structure.
        Bearish OB: Last up-close candle preceding strong downward displacement breaking structure.
        """
        obs: List[OrderBlock] = []
        n = len(bars)
        if n < 5:
            return obs

        for i in range(1, n - 2):
            curr = bars[i]
            next1 = bars[i + 1]
            next2 = bars[i + 2]

            # Check displacement (two subsequent candles moving aggressively)
            bullish_disp = (next1.close > next1.open) and (next2.close > next2.open) and \
                           ((next2.close - curr.close) > 1.5 * atr)
            bearish_disp = (next1.close < next1.open) and (next2.close < next2.open) and \
                           ((curr.close - next2.close) > 1.5 * atr)

            # Bullish OB: down candle before strong bullish expansion
            if curr.close < curr.open and bullish_disp:
                ob = OrderBlock(
                    direction="BULLISH",
                    high=curr.high,
                    low=curr.low,
                    open=curr.open,
                    close=curr.close,
                    bar_index=i,
                    time=curr.time,
                    volume_ratio=curr.volume / (bars[i-1].volume + 1e-6)
                )
                # Check mitigation
                for j in range(i + 3, n):
                    if bars[j].low <= ob.high:
                        if bars[j].close < ob.low:
                            ob.is_mitigated = True
                            break
                obs.append(ob)

            # Bearish OB: up candle before strong bearish expansion
            elif curr.close > curr.open and bearish_disp:
                ob = OrderBlock(
                    direction="BEARISH",
                    high=curr.high,
                    low=curr.low,
                    open=curr.open,
                    close=curr.close,
                    bar_index=i,
                    time=curr.time,
                    volume_ratio=curr.volume / (bars[i-1].volume + 1e-6)
                )
                for j in range(i + 3, n):
                    if bars[j].high >= ob.low:
                        if bars[j].close > ob.high:
                            ob.is_mitigated = True
                            break
                obs.append(ob)

        return obs

    def find_swing_points(self, bars: List[Bar], lookback: int = 3) -> List[SwingPoint]:
        """Detects fractal swing highs and lows."""
        swings: List[SwingPoint] = []
        n = len(bars)
        if n < lookback * 2 + 1:
            return swings

        for i in range(lookback, n - lookback):
            # Swing High: Highest in +/- lookback window
            is_high = all(bars[i].high >= bars[i - k].high and bars[i].high >= bars[i + k].high 
                          for k in range(1, lookback + 1))
            if is_high:
                swings.append(SwingPoint("HIGH", bars[i].high, i, bars[i].time))

            # Swing Low: Lowest in +/- lookback window
            is_low = all(bars[i].low <= bars[i - k].low and bars[i].low <= bars[i + k].low 
                         for k in range(1, lookback + 1))
            if is_low:
                swings.append(SwingPoint("LOW", bars[i].low, i, bars[i].time))

        return swings

    def analyze_market_structure(self, bars: List[Bar], swings: List[SwingPoint]) -> Tuple[str, str]:
        """
        Determines Higher-Timeframe Trend and Structure Shifts (BOS / MSS).
        """
        if not swings or len(swings) < 4:
            return "RANGE", "NEUTRAL"

        highs = [s for s in swings if s.type == "HIGH"]
        lows = [s for s in swings if s.type == "LOW"]

        if len(highs) < 2 or len(lows) < 2:
            return "RANGE", "NEUTRAL"

        recent_highs = highs[-2:]
        recent_lows = lows[-2:]
        curr_price = bars[-1].close

        # Trend derivation
        is_bullish = recent_highs[-1].price > recent_highs[-2].price and recent_lows[-1].price > recent_lows[-2].price
        is_bearish = recent_highs[-1].price < recent_highs[-2].price and recent_lows[-1].price < recent_lows[-2].price

        trend = "BULLISH" if is_bullish else ("BEARISH" if is_bearish else "RANGE")

        # Market Structure Shift (MSS) vs Break of Structure (BOS)
        structure = "NEUTRAL"
        last_high = highs[-1].price
        last_low = lows[-1].price

        if curr_price > last_high:
            structure = "BOS_BULLISH" if trend == "BULLISH" else "MSS_BULLISH"
        elif curr_price < last_low:
            structure = "BOS_BEARISH" if trend == "BEARISH" else "MSS_BEARISH"

        return trend, structure

    def calculate_atr(self, bars: List[Bar], period: int = 14) -> float:
        """Calculates Average True Range (ATR)."""
        if len(bars) < period + 1:
            return 1.0
        trs = []
        for i in range(1, len(bars)):
            h_l = bars[i].high - bars[i].low
            h_pc = abs(bars[i].high - bars[i - 1].close)
            l_pc = abs(bars[i].low - bars[i - 1].close)
            trs.append(max(h_l, h_pc, l_pc))
        recent_trs = trs[-period:]
        return sum(recent_trs) / len(recent_trs)

    def extract_features(
        self,
        bars_m5: List[Bar],
        bars_m15: List[Bar],
        bars_h1: List[Bar],
        spread_points: float = 1.0
    ) -> SMCFeatures:
        """
        Orchestrates multi-timeframe feature extraction and returns SMCFeatures object.
        """
        now_utc = bars_m5[-1].time if bars_m5 else datetime.now(timezone.utc)
        curr_price = bars_m5[-1].close if bars_m5 else 0.0

        # 1. Session & Liquidity
        current_session = self.get_current_session(now_utc)
        asian_liq = self.compute_asian_session_liquidity(bars_m15)
        bsl_swept = asian_liq.high_swept
        ssl_swept = asian_liq.low_swept

        # 2. ATR
        atr_m5 = self.calculate_atr(bars_m5, 14)

        # 3. Market Structure Multi-Timeframe
        swings_h1 = self.find_swing_points(bars_h1, lookback=3)
        h1_trend, _ = self.analyze_market_structure(bars_h1, swings_h1)

        swings_m15 = self.find_swing_points(bars_m15, lookback=3)
        _, m15_struct = self.analyze_market_structure(bars_m15, swings_m15)

        swings_m5 = self.find_swing_points(bars_m5, lookback=2)
        _, m5_struct = self.analyze_market_structure(bars_m5, swings_m5)

        # 4. Dealing Range & Premium/Discount
        recent_highs = [s.price for s in swings_m15 if s.type == "HIGH"]
        recent_lows = [s.price for s in swings_m15 if s.type == "LOW"]
        range_high = max(recent_highs[-3:]) if recent_highs else curr_price * 1.01
        range_low = min(recent_lows[-3:]) if recent_lows else curr_price * 0.99

        range_span = max(range_high - range_low, 1e-4)
        eq_price = range_low + (range_span * 0.5)
        fib_pct = ((curr_price - range_low) / range_span) * 100.0

        if fib_pct < 45.0:
            price_zone = "DISCOUNT"
        elif fib_pct > 55.0:
            price_zone = "PREMIUM"
        else:
            price_zone = "EQUILIBRIUM"

        # 5. SMC Arrays (FVG & OB)
        fvgs = self.detect_fvgs(bars_m5, min_gap_points=0.2)
        untested_fvgs = [f for f in fvgs if not f.is_mitigated]
        active_fvg = untested_fvgs[-1] if untested_fvgs else None

        obs = self.detect_order_blocks(bars_m5, atr_m5)
        untested_obs = [o for o in obs if not o.is_mitigated]
        active_ob = untested_obs[-1] if untested_obs else None

        # Targets
        nearest_bsl = range_high
        nearest_ssl = range_low

        # 6. Displacement & Volume
        displacement = False
        volume_expansion = False
        if len(bars_m5) >= 5:
            last_candle_body = abs(bars_m5[-1].close - bars_m5[-1].open)
            displacement = last_candle_body > (1.2 * atr_m5)
            avg_vol = sum(b.volume for b in bars_m5[-6:-1]) / 5.0
            volume_expansion = bars_m5[-1].volume > (1.3 * avg_vol)

        return SMCFeatures(
            symbol=self.symbol,
            timestamp=now_utc,
            current_price=curr_price,
            spread_points=spread_points,
            current_session=current_session,
            asian_liquidity=asian_liq,
            bsl_swept=bsl_swept,
            ssl_swept=ssl_swept,
            h1_trend=h1_trend,
            m15_structure=m15_struct,
            m5_structure=m5_struct,
            dealing_range_high=round(range_high, 2),
            dealing_range_low=round(range_low, 2),
            equilibrium_price=round(eq_price, 2),
            price_zone=price_zone,
            fib_retracement_pct=round(fib_pct, 1),
            active_m5_fvg=active_fvg,
            active_m5_ob=active_ob,
            nearest_bsl_target=round(nearest_bsl, 2),
            nearest_ssl_target=round(nearest_ssl, 2),
            atr_m5=round(atr_m5, 2),
            displacement_detected=displacement,
            volume_expansion=volume_expansion
        )
