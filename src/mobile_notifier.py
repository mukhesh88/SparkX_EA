"""
SparkX EA - Mobile Notification Dispatcher
Sends real-time institutional trade signals and fill receipts to your phone
via Telegram Bot and Discord Webhook.
"""

import os
import json
import logging
import urllib.request
from typing import Optional, Dict, Any

logger = logging.getLogger("SparkX.MobileNotifier")

CONFIG_PATH = os.path.join(os.path.dirname(__file__), "..", "config", "mobile_alerts.json")


class MobileAlertDispatcher:
    def __init__(self, config_path: str = CONFIG_PATH):
        self.config_path = os.path.abspath(config_path)
        self.telegram_enabled = False
        self.telegram_token = ""
        self.telegram_chat_id = ""
        self.discord_enabled = False
        self.discord_webhook = ""
        self.load_config()

    def load_config(self):
        """Loads credentials from config/mobile_alerts.json or environment variables."""
        # 1. Check environment variables first
        self.telegram_token = os.getenv("TELEGRAM_BOT_TOKEN", "")
        self.telegram_chat_id = os.getenv("TELEGRAM_CHAT_ID", "")
        self.discord_webhook = os.getenv("DISCORD_WEBHOOK_URL", "")

        # 2. Check config JSON file
        if os.path.exists(self.config_path):
            try:
                with open(self.config_path, "r", encoding="utf-8") as f:
                    cfg = json.load(f)
                    tg = cfg.get("telegram", {})
                    if tg.get("enabled"):
                        self.telegram_enabled = True
                        if tg.get("bot_token") and tg["bot_token"] != "YOUR_TELEGRAM_BOT_TOKEN":
                            self.telegram_token = tg["bot_token"]
                        if tg.get("chat_id") and tg["chat_id"] != "YOUR_TELEGRAM_CHAT_ID":
                            self.telegram_chat_id = tg["chat_id"]

                    dc = cfg.get("discord", {})
                    if dc.get("enabled"):
                        self.discord_enabled = True
                        if dc.get("webhook_url") and "discord.com/api/webhooks" in dc["webhook_url"]:
                            self.discord_webhook = dc["webhook_url"]
            except Exception as e:
                logger.warning(f"Failed to parse {self.config_path}: {e}")

        if self.telegram_token and self.telegram_chat_id:
            self.telegram_enabled = True
        if self.discord_webhook:
            self.discord_enabled = True

        if self.telegram_enabled:
            logger.info("Mobile Notifications: Telegram Alerts ACTIVE")
        if self.discord_enabled:
            logger.info("Mobile Notifications: Discord Webhook ACTIVE")

    def send_trade_signal(
        self,
        symbol: str,
        action: str,
        price: float,
        sl: float,
        tp: float,
        score: float,
        confidence: float,
        reason: str,
        order_id: Optional[int] = None
    ):
        """Dispatches an instant mobile signal for a new SMC trade entry."""
        if not (self.telegram_enabled or self.discord_enabled):
            return

        is_buy = "BUY" in action.upper()
        emoji = "🟢" if is_buy else "🔴"
        action_name = "STRONG BUY (LONG)" if is_buy else "STRONG SELL (SHORT)"
        rr = abs(tp - price) / max(abs(price - sl), 0.01)

        # 1. Dispatch Telegram Alert
        if self.telegram_enabled and self.telegram_token and self.telegram_chat_id:
            tg_text = (
                f"{emoji} <b>SPARKX EA // AI SIGNAL</b>\n\n"
                f"<b>Asset:</b> <code>{symbol}</code>\n"
                f"<b>Action:</b> <b>{action_name}</b>\n"
                f"<b>Entry Price:</b> <code>${price:,.2f}</code>\n"
                f"<b>Stop Loss:</b> <code>${sl:,.2f}</code>\n"
                f"<b>Take Profit:</b> <code>${tp:,.2f}</code>\n"
                f"<b>Risk:Reward:</b> <code>1:{rr:.1f}</code>\n"
                f"<b>Setup Score:</b> <code>{score:.1f}/10.0</code>\n"
                f"<b>Confidence:</b> <code>{confidence * 100:.1f}%</code>\n\n"
                f"<b>SMC Logic:</b> <i>{reason}</i>\n"
            )
            if order_id:
                tg_text += f"\n✅ <b>MT5 Executed:</b> Ticket #{order_id}"

            self._send_telegram(tg_text)

        # 2. Dispatch Discord Alert
        if self.discord_enabled and self.discord_webhook:
            color = 0x00FF66 if is_buy else 0xFF003C
            embed = {
                "title": f"{emoji} SparkX EA Signal: {symbol} - {action}",
                "color": color,
                "fields": [
                    {"name": "Action", "value": action_name, "inline": True},
                    {"name": "Entry Price", "value": f"${price:,.2f}", "inline": True},
                    {"name": "Risk : Reward", "value": f"1 : {rr:.1f}", "inline": True},
                    {"name": "Stop Loss (SL)", "value": f"${sl:,.2f}", "inline": True},
                    {"name": "Take Profit (TP)", "value": f"${tp:,.2f}", "inline": True},
                    {"name": "Setup Score", "value": f"{score:.1f} / 10.0", "inline": True},
                    {"name": "SMC Confluence", "value": reason, "inline": False}
                ],
                "footer": {"text": "SparkX EA // Laya ONNX Ultra-Low-Latency Engine"}
            }
            if order_id:
                embed["fields"].append({"name": "Broker Execution", "value": f"Filled MT5 Ticket #{order_id}", "inline": False})

            self._send_discord({"embeds": [embed]})

    def _send_telegram(self, html_text: str):
        url = f"https://api.telegram.org/bot{self.telegram_token}/sendMessage"
        payload = json.dumps({
            "chat_id": self.telegram_chat_id,
            "text": html_text,
            "parse_mode": "HTML",
            "disable_web_page_preview": True
        }).encode("utf-8")
        try:
            req = urllib.request.Request(url, data=payload, headers={"Content-Type": "application/json"})
            with urllib.request.urlopen(req, timeout=3.0) as resp:
                pass
        except Exception as e:
            logger.warning(f"Telegram notification dispatch error: {e}")

    def _send_discord(self, payload_dict: dict):
        payload = json.dumps(payload_dict).encode("utf-8")
        try:
            req = urllib.request.Request(self.discord_webhook, data=payload, headers={
                "Content-Type": "application/json",
                "User-Agent": "SparkXTerminal/1.0"
            })
            with urllib.request.urlopen(req, timeout=3.0) as resp:
                pass
        except Exception as e:
            logger.warning(f"Discord notification dispatch error: {e}")


# Singleton instance
mobile_notifier = MobileAlertDispatcher()
