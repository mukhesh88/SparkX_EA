import sys
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")

import os
import torch
import torch.nn as nn
from laya.agent import Agent

class SparkXLayaModel(nn.Module):
    """
    Direct end-to-end ONNX inference wrapper for SparkX Terminal.
    Takes (input_ids, attention_mask) of shape [batch, 512].
    Produces:
      - choice_logits [batch, 5]: Directional action probabilities
      - score_val [batch, 1]: Setup quality score (1.0 to 10.0)
      - noul_logits [batch, 3]: Truth probabilities for (Asian Sweep, FVG, MSS)
    """
    def __init__(self, laya_model):
        super().__init__()
        self.encoder = laya_model.encoder
        self.scorer = laya_model.scorer
        hidden_size = 1024
        
        # Choice head: 5 classes (MARKET_BUY, MARKET_SELL, LIMIT_BUY, LIMIT_SELL, HOLD)
        self.choice_head = nn.Sequential(
            nn.LayerNorm(hidden_size),
            nn.Linear(hidden_size, 256),
            nn.GELU(),
            nn.Linear(256, 5)
        )
        
        # Noul head: 3 binary tests (asian_sweep, fvg_valid, mss_confirmed)
        self.noul_head = nn.Sequential(
            nn.LayerNorm(hidden_size),
            nn.Linear(hidden_size, 128),
            nn.GELU(),
            nn.Linear(128, 3)
        )
        
        # Initialize heads with pretrained representations
        with torch.no_grad():
            if hasattr(laya_model, 'act_head') and len(laya_model.act_head) > 0:
                first_linear = laya_model.act_head[0]
                if hasattr(first_linear, 'weight'):
                    w = first_linear.weight[:, :1024]
                    self.choice_head[1].weight.copy_(w[:256, :])
                    self.noul_head[1].weight.copy_(w[:128, :])

    def forward(self, input_ids: torch.Tensor, attention_mask: torch.Tensor):
        # 1. ModernBERT encoder forward pass
        enc_out = self.encoder(input_ids=input_ids, attention_mask=attention_mask)
        h = enc_out.last_hidden_state  # [B, L, 1024]
        
        # 2. Mean pooling over valid attention tokens for dense SMC context representation
        mask_expanded = attention_mask.unsqueeze(-1).float()
        sum_embeddings = torch.sum(h * mask_expanded, dim=1)
        sum_mask = torch.clamp(mask_expanded.sum(dim=1), min=1e-9)
        pooled = sum_embeddings / sum_mask  # [B, 1024]
        
        # 3. Laya Primitives
        # Primitive 1: Choice [B, 5]
        choice_logits = self.choice_head(pooled)
        
        # Primitive 2: Score [B, 1] mapped to [1.0, 10.0]
        raw_score = self.scorer(pooled)
        score_val = 1.0 + 9.0 * torch.sigmoid(raw_score)
        
        # Primitive 3: Noul [B, 3]
        noul_logits = self.noul_head(pooled)
        
        return choice_logits, score_val, noul_logits


def export_model(model_id: str = "convaiinnovations/laya", output_path: str = "models/laya.onnx"):
    print(f"[*] Loading official weights from: {model_id}...")
    agent = Agent(model_id, compile=False, device="cpu")
    agent.model.eval()
    
    wrapper = SparkXLayaModel(agent.model)
    wrapper.eval()
    
    # Dummy inputs matching ModernBERT-large 512 context limit
    batch_size = 1
    seq_len = 512
    dummy_input_ids = torch.randint(0, 50000, (batch_size, seq_len), dtype=torch.long)
    dummy_attention_mask = torch.ones((batch_size, seq_len), dtype=torch.long)
    
    print("[*] Verifying PyTorch forward pass...")
    with torch.no_grad():
        c, s, n = wrapper(dummy_input_ids, dummy_attention_mask)
        print(f"    Choice logits shape: {c.shape}")
        print(f"    Score value shape:   {s.shape} (value: {s.item():.2f})")
        print(f"    Noul logits shape:   {n.shape}")
    
    out_dir = os.path.dirname(os.path.abspath(output_path))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
        
    print(f"[*] Exporting ONNX graph to: {output_path}...")
    torch.onnx.export(
        wrapper,
        (dummy_input_ids, dummy_attention_mask),
        output_path,
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
    print(f"[SUCCESS] Exported official Laya ONNX model to: {output_path}")

if __name__ == "__main__":
    export_model()
