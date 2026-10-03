"""
SparkX EA - Laya AI Institutional Model Training Pipeline
Fine-tunes the ModernBERT-based Laya AI model on multi-timeframe Smart Money Concepts (SMC)
data using institutional triple-barrier labeling, and exports the optimized ONNX model.
"""

import sys
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")

import os
import argparse
import time
import math
import random
import logging
from datetime import datetime, timezone, timedelta
from typing import List, Tuple, Dict, Any

import torch
import torch.nn as nn
from torch.utils.data import Dataset, DataLoader
from transformers import AutoTokenizer

# Ensure workspace root is in sys.path
WORKSPACE_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if WORKSPACE_ROOT not in sys.path:
    sys.path.insert(0, WORKSPACE_ROOT)

from laya.agent import Agent
from scripts.export_sparkx_laya_onnx import SparkXLayaModel
from src.smc_engine import SMCEngine, Bar, SMCFeatures
from src.compressor import MarketStateCompressor

try:
    import MetaTrader5 as mt5
    MT5_AVAILABLE = True
except ImportError:
    MT5_AVAILABLE = False
    mt5 = None

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] [LayaTrainer] %(message)s'
)
logger = logging.getLogger("SparkX.LayaTrainer")


class SMCTradingDataset(Dataset):
    """PyTorch Dataset holding pre-tokenized <MKT_STATE> strings and institutional ground-truth labels."""
    def __init__(self, states: List[str], choices: List[int], scores: List[float], nouls: List[List[float]], tokenizer, max_len: int = 192):
        self.choices = torch.tensor(choices, dtype=torch.long)
        self.scores = torch.tensor(scores, dtype=torch.float32).unsqueeze(1)
        self.nouls = torch.tensor(nouls, dtype=torch.float32)
        
        # Batch tokenize all states simultaneously (sub-second on entire dataset)
        encodings = tokenizer(
            states,
            max_length=max_len,
            padding=True,
            truncation=True,
            return_tensors="pt"
        )
        self.input_ids = encodings["input_ids"]
        self.attention_mask = encodings["attention_mask"]

    def __len__(self):
        return len(self.choices)

    def __getitem__(self, idx):
        return {
            "input_ids": self.input_ids[idx],
            "attention_mask": self.attention_mask[idx],
            "choice": self.choices[idx],
            "score": self.scores[idx],
            "noul": self.nouls[idx]
        }


def fetch_historical_bars(symbol: str, count: int = 2000) -> Tuple[List[Bar], List[Bar], List[Bar]]:
    """Fetches M5, M15, and H1 bars from MT5 if connected, or synthesizes institutional market data."""
    bars_m5, bars_m15, bars_h1 = [], [], []
    
    if MT5_AVAILABLE and mt5 is not None and mt5.initialize():
        logger.info(f"Connected to MetaTrader 5. Fetching {count} historical bars for {symbol}...")
        rates_m5 = mt5.copy_rates_from_pos(symbol, mt5.TIMEFRAME_M5, 0, count)
        rates_m15 = mt5.copy_rates_from_pos(symbol, mt5.TIMEFRAME_M15, 0, count // 3 + 50)
        rates_h1 = mt5.copy_rates_from_pos(symbol, mt5.TIMEFRAME_H1, 0, count // 12 + 50)
        
        if rates_m5 is not None and len(rates_m5) > 100:
            for r in rates_m5:
                bars_m5.append(Bar(
                    time=datetime.fromtimestamp(r['time'], timezone.utc),
                    open=float(r['open']), high=float(r['high']), low=float(r['low']),
                    close=float(r['close']), volume=float(r['tick_volume'])
                ))
            if rates_m15 is not None:
                for r in rates_m15:
                    bars_m15.append(Bar(
                        time=datetime.fromtimestamp(r['time'], timezone.utc),
                        open=float(r['open']), high=float(r['high']), low=float(r['low']),
                        close=float(r['close']), volume=float(r['tick_volume'])
                    ))
            if rates_h1 is not None:
                for r in rates_h1:
                    bars_h1.append(Bar(
                        time=datetime.fromtimestamp(r['time'], timezone.utc),
                        open=float(r['open']), high=float(r['high']), low=float(r['low']),
                        close=float(r['close']), volume=float(r['tick_volume'])
                    ))
            logger.info(f"Loaded from MT5: M5={len(bars_m5)}, M15={len(bars_m15)}, H1={len(bars_h1)}")
            return bars_m5, bars_m15, bars_h1

    logger.warning("MT5 not active. Generating synthetic institutional multi-timeframe SMC dataset...")
    base_px = 2360.0
    now = datetime.now(timezone.utc) - timedelta(minutes=5 * count)
    
    for i in range(count):
        t = now + timedelta(minutes=5 * i)
        cycle = math.sin(i / 40.0) * 15.0 + math.cos(i / 150.0) * 25.0
        drift = (random.random() - 0.495) * 3.5
        open_p = round(base_px + cycle + drift, 2)
        spread = random.uniform(1.2, 5.0)
        high_p = round(open_p + spread + random.uniform(0.5, 3.0), 2)
        low_p = round(open_p - random.uniform(0.5, 3.0), 2)
        close_p = round(random.uniform(low_p, high_p), 2)
        vol = random.uniform(100.0, 1500.0)
        
        bars_m5.append(Bar(t, open_p, high_p, low_p, close_p, vol))
        
        # Aggregate M15 every 3 bars
        if i % 3 == 0:
            bars_m15.append(Bar(t, open_p, high_p, low_p, close_p, vol * 2.8))
        # Aggregate H1 every 12 bars
        if i % 12 == 0:
            bars_h1.append(Bar(t, open_p, high_p, low_p, close_p, vol * 11.5))
            
    return bars_m5, bars_m15, bars_h1


def generate_labeled_dataset(
    bars_m5: List[Bar],
    bars_m15: List[Bar],
    bars_h1: List[Bar],
    symbol: str = "XAUUSD",
    horizon_bars: int = 24,
    sl_points: float = 4.5,
    tp_points: float = 12.0
) -> Tuple[List[str], List[int], List[float], List[List[float]]]:
    """
    Extracts SMC features for each timestamp and assigns institutional ground-truth labels
    using the triple-barrier method:
      Choice: 0=BUY, 1=SELL, 2=LIMIT_BUY_OB, 3=LIMIT_SELL_OB, 4=HOLD
      Score: 1.0 to 10.0
      Noul: [asian_swept, fvg_fresh, mss_disp]
    """
    smc = SMCEngine(symbol=symbol)
    states = []
    choices = []
    scores = []
    nouls = []
    
    total_bars = len(bars_m5)
    logger.info(f"Extracting SMC features and labeling {total_bars - horizon_bars - 60} training samples...")

    for i in range(60, total_bars - horizon_bars):
        sub_m5 = bars_m5[:i]
        sub_m15 = [b for b in bars_m15 if b.time <= sub_m5[-1].time]
        sub_h1 = [b for b in bars_h1 if b.time <= sub_m5[-1].time]
        
        if len(sub_m15) < 15 or len(sub_h1) < 10:
            continue
            
        features: SMCFeatures = smc.extract_features(sub_m5[-60:], sub_m15[-40:], sub_h1[-40:], spread_points=1.5)
        compressed = MarketStateCompressor.compress(features)
        
        cur_px = features.current_price
        future_bars = bars_m5[i:i + horizon_bars]
        
        # Triple-barrier outcome simulation
        max_high = max(b.high for b in future_bars)
        min_low = min(b.low for b in future_bars)
        
        # Check Long setup
        buy_tp_hit = False
        buy_sl_hit = False
        for b in future_bars:
            if b.low <= (cur_px - sl_points):
                buy_sl_hit = True
                break
            if b.high >= (cur_px + tp_points):
                buy_tp_hit = True
                break
                
        # Check Short setup
        sell_tp_hit = False
        sell_sl_hit = False
        for b in future_bars:
            if b.high >= (cur_px + sl_points):
                sell_sl_hit = True
                break
            if b.low <= (cur_px - tp_points):
                sell_tp_hit = True
                break

        # Institutional Trader Rules Filtering:
        # A valid BUY requires Discount + (Sweep or Displacement on FVG/OB)
        # A valid SELL requires Premium + (Sweep or Displacement on FVG/OB)
        is_discount = features.price_zone == "DISCOUNT"
        is_premium = features.price_zone == "PREMIUM"
        has_trigger_buy = features.ssl_swept or (features.displacement_detected and features.active_m5_fvg)
        has_trigger_sell = features.bsl_swept or (features.displacement_detected and features.active_m5_fvg)
        
        if buy_tp_hit and not buy_sl_hit and is_discount and has_trigger_buy:
            choice = 0 # MARKET_BUY
            score = round(random.uniform(8.2, 9.8), 2)
        elif sell_tp_hit and not sell_sl_hit and is_premium and has_trigger_sell:
            choice = 1 # MARKET_SELL
            score = round(random.uniform(8.2, 9.8), 2)
        elif features.active_m5_ob and is_discount and features.active_m5_ob.direction == "BULLISH":
            choice = 2 # LIMIT_BUY_ORDER_BLOCK
            score = round(random.uniform(7.0, 8.5), 2)
        elif features.active_m5_ob and is_premium and features.active_m5_ob.direction == "BEARISH":
            choice = 3 # LIMIT_SELL_ORDER_BLOCK
            score = round(random.uniform(7.0, 8.5), 2)
        else:
            choice = 4 # HOLD
            score = round(random.uniform(2.0, 5.5), 2)
            
        noul = [
            1.0 if (features.ssl_swept or features.bsl_swept) else 0.0,
            1.0 if features.active_m5_fvg is not None else 0.0,
            1.0 if features.displacement_detected else 0.0
        ]
        
        states.append(compressed)
        choices.append(choice)
        scores.append(score)
        nouls.append(noul)

    logger.info(f"Generated {len(states)} labeled samples (BUY: {choices.count(0)}, SELL: {choices.count(1)}, HOLD: {choices.count(4)})")
    return states, choices, scores, nouls


def train_laya(
    symbol: str = "XAUUSD",
    bars_count: int = 1500,
    epochs: int = 5,
    batch_size: int = 8,
    lr: float = 2e-5,
    output_onnx_path: str = "models/laya.onnx",
    device_name: str = "cpu",
    unfreeze_encoder: bool = False
):
    device = torch.device(device_name if torch.cuda.is_available() and device_name == "cuda" else "cpu")
    logger.info(f"Initializing Laya Model Training Pipeline on: {device}")
    
    # 1. Fetch bars & generate labeled SMC dataset
    bars_m5, bars_m15, bars_h1 = fetch_historical_bars(symbol, count=bars_count)
    states, choices, scores, nouls = generate_labeled_dataset(bars_m5, bars_m15, bars_h1, symbol=symbol)
    
    # 2. Load Tokenizer & Model
    tokenizer_path = os.path.abspath("models/tokenizer")
    if os.path.exists(tokenizer_path):
        tokenizer = AutoTokenizer.from_pretrained(tokenizer_path)
    else:
        tokenizer = AutoTokenizer.from_pretrained("convaiinnovations/laya")
        
    dataset = SMCTradingDataset(states, choices, scores, nouls, tokenizer, max_len=192)
    train_loader = DataLoader(dataset, batch_size=batch_size, shuffle=True)
    
    logger.info("Loading pretrained ModernBERT Laya agent...")
    agent = Agent("convaiinnovations/laya", compile=False, device="cpu")
    model = SparkXLayaModel(agent.model).to(device)
    model.train()
    
    # 3. Training Loop (Fast Feature Extraction Mode vs Full Fine-Tuning)
    class_weights = torch.tensor([3.5, 3.5, 2.0, 2.0, 0.8], device=device)
    criterion_choice = nn.CrossEntropyLoss(weight=class_weights)
    criterion_score = nn.SmoothL1Loss()
    criterion_noul = nn.BCEWithLogitsLoss()

    if not unfreeze_encoder:
        logger.info(f"Fast Mode: Pre-computing ModernBERT embeddings for {len(dataset)} samples...")
        model.eval()
        embeddings_list = []
        total_batches = math.ceil(len(dataset) / 16)
        with torch.no_grad():
            for i in range(0, len(dataset), 16):
                b_ids = dataset.input_ids[i:i + 16].to(device)
                b_mask = dataset.attention_mask[i:i + 16].to(device)
                enc_out = model.encoder(input_ids=b_ids, attention_mask=b_mask)
                h = enc_out.last_hidden_state
                mask_exp = b_mask.unsqueeze(-1).float()
                sum_emb = torch.sum(h * mask_exp, dim=1)
                sum_m = torch.clamp(mask_exp.sum(dim=1), min=1e-9)
                pooled = sum_emb / sum_m
                embeddings_list.append(pooled.cpu())
                
                batch_num = (i // 16) + 1
                if batch_num % 5 == 0 or batch_num == total_batches:
                    pct = (batch_num / total_batches) * 100.0
                    logger.info(f"Feature Extraction: {batch_num}/{total_batches} batches ({pct:.0f}%)")
                
        all_embeddings = torch.cat(embeddings_list, dim=0).to(device)
        logger.info(f"Extracted {all_embeddings.shape[0]} latent representations ({all_embeddings.shape[1]}-dim).")

        head_dataset = torch.utils.data.TensorDataset(
            all_embeddings,
            dataset.choices.to(device),
            dataset.scores.to(device),
            dataset.nouls.to(device)
        )
        head_loader = DataLoader(head_dataset, batch_size=batch_size, shuffle=True)
        head_params = list(model.choice_head.parameters()) + list(model.scorer.parameters()) + list(model.noul_head.parameters())
        optimizer = torch.optim.AdamW(head_params, lr=lr * 5, weight_decay=0.01)

        model.choice_head.train()
        model.scorer.train()
        model.noul_head.train()

        logger.info(f"Training Decision Heads: {epochs} Epochs, Batch Size {batch_size}...")
        for epoch in range(1, epochs + 1):
            total_loss = 0.0
            correct = 0
            total = 0
            t0 = time.time()
            for emb, choice, score, noul in head_loader:
                optimizer.zero_grad()
                choice_logits = model.choice_head(emb)
                raw_score = model.scorer(emb)
                score_val = 1.0 + 9.0 * torch.sigmoid(raw_score)
                noul_logits = model.noul_head(emb)

                loss_c = criterion_choice(choice_logits, choice)
                loss_s = criterion_score(score_val, score)
                loss_n = criterion_noul(noul_logits, noul)
                loss = loss_c + 0.5 * loss_s + 0.3 * loss_n
                loss.backward()
                optimizer.step()

                total_loss += loss.item() * len(choice)
                preds = torch.argmax(choice_logits, dim=-1)
                correct += (preds == choice).sum().item()
                total += len(choice)

            elapsed = time.time() - t0
            avg_loss = total_loss / max(1, total)
            acc = (correct / max(1, total)) * 100.0
            logger.info(f"Epoch {epoch}/{epochs} | Loss: {avg_loss:.4f} | Choice Acc: {acc:.1f}% | Time: {elapsed:.2f}s")
    else:
        logger.info(f"Full End-to-End Fine-Tuning: {epochs} Epochs, Batch Size {batch_size}, LR {lr}...")
        optimizer = torch.optim.AdamW(model.parameters(), lr=lr, weight_decay=0.01)
        for epoch in range(1, epochs + 1):
            total_loss = 0.0
            correct = 0
            total = 0
            t0 = time.time()
            for batch in train_loader:
                input_ids = batch["input_ids"].to(device)
                attention_mask = batch["attention_mask"].to(device)
                target_choice = batch["choice"].to(device)
                target_score = batch["score"].to(device)
                target_noul = batch["noul"].to(device)

                optimizer.zero_grad()
                choice_logits, score_val, noul_logits = model(input_ids, attention_mask)

                loss_c = criterion_choice(choice_logits, target_choice)
                loss_s = criterion_score(score_val, target_score)
                loss_n = criterion_noul(noul_logits, target_noul)
                loss = loss_c + 0.5 * loss_s + 0.3 * loss_n
                loss.backward()
                torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
                optimizer.step()

                total_loss += loss.item() * len(target_choice)
                preds = torch.argmax(choice_logits, dim=-1)
                correct += (preds == target_choice).sum().item()
                total += len(target_choice)

            elapsed = time.time() - t0
            avg_loss = total_loss / max(1, total)
            acc = (correct / max(1, total)) * 100.0
            logger.info(f"Epoch {epoch}/{epochs} | Loss: {avg_loss:.4f} | Choice Acc: {acc:.1f}% | Time: {elapsed:.1f}s")

    logger.info("Training complete. Exporting fine-tuned model to ONNX...")
    model.eval()

    # 4. Export to ONNX
    dummy_input_ids = torch.randint(0, 50000, (1, 512), dtype=torch.long, device=device)
    dummy_attention_mask = torch.ones((1, 512), dtype=torch.long, device=device)

    os.makedirs(os.path.dirname(os.path.abspath(output_onnx_path)), exist_ok=True)

    torch.onnx.export(
        model,
        (dummy_input_ids, dummy_attention_mask),
        output_onnx_path,
        export_params=True,
        opset_version=18,
        do_constant_folding=True,
        input_names=["input_ids", "attention_mask"],
        output_names=["choice_logits", "score_grade", "noul_logits"],
        dynamic_axes={
            "input_ids": {0: "batch_size", 1: "seq_len"},
            "attention_mask": {0: "batch_size", 1: "seq_len"},
            "choice_logits": {0: "batch_size"},
            "score_grade": {0: "batch_size"},
            "noul_logits": {0: "batch_size"}
        }
    )
    logger.info(f"[SUCCESS] Exported trained model to {output_onnx_path}")

    # Copy to release folder if exists
    release_path = os.path.abspath("release/SparkX_Terminal/models/laya.onnx")
    if os.path.exists(os.path.dirname(release_path)):
        import shutil
        shutil.copy2(output_onnx_path, release_path)
        logger.info(f"[SUCCESS] Synchronized trained model to standalone release package: {release_path}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Train and fine-tune SparkX Laya AI model.")
    parser.add_argument("--symbol", type=str, default="XAUUSD", help="Symbol to train on (e.g., XAUUSD)")
    parser.add_argument("--bars", type=int, default=1500, help="Number of historical bars to process")
    parser.add_argument("--epochs", type=int, default=3, help="Number of training epochs")
    parser.add_argument("--batch-size", type=int, default=8, help="Batch size")
    parser.add_argument("--lr", type=float, default=2e-5, help="Learning rate")
    parser.add_argument("--device", type=str, default="cpu", help="Device (cpu or cuda)")
    parser.add_argument("--unfreeze-encoder", action="store_true", help="Unfreeze ModernBERT encoder backbone for full end-to-end training (requires GPU)")
    parser.add_argument("--output", type=str, default="models/laya.onnx", help="Output ONNX path")
    args = parser.parse_args()

    train_laya(
        symbol=args.symbol,
        bars_count=args.bars,
        epochs=args.epochs,
        batch_size=args.batch_size,
        lr=args.lr,
        output_onnx_path=args.output,
        device_name=args.device,
        unfreeze_encoder=args.unfreeze_encoder
    )
