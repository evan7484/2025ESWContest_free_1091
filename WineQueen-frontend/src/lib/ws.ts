// 전역 WebSocket 싱글턴 + 자동 재연결 + 하트비트
let ws: WebSocket | null = null;
let lastUrl = "";
let listeners = new Set<(e: MessageEvent) => void>();
let heartbeatTimer: number | null = null;
let reconnectDelay = 1000; // 1s → 2s → 4s … 최대 15s

function connect(url: string) {
  lastUrl = url;
  ws = new WebSocket(url);

  ws.addEventListener("open", () => {
    console.log("✅ WS connected:", url);
    reconnectDelay = 1000;
    if (heartbeatTimer) clearInterval(heartbeatTimer);
    heartbeatTimer = window.setInterval(() => {
      if (ws?.readyState === WebSocket.OPEN) ws.send("ping");
    }, 5000);
  });

  ws.addEventListener("message", (e) => {
    for (const l of listeners) l(e);
  });

  ws.addEventListener("error", (e) => {
    console.warn("WS error:", e);
    try {
      ws?.close();
    } catch {}
  });

  ws.addEventListener("close", () => {
    console.log("❌ WS closed");
    if (heartbeatTimer) {
      clearInterval(heartbeatTimer);
      heartbeatTimer = null;
    }
    // 자동 재연결 (지수 백오프, 최대 15s)
    setTimeout(() => connect(lastUrl), reconnectDelay);
    reconnectDelay = Math.min(reconnectDelay * 2, 15000);
  });
}

export function subscribeWS(
  url: string,
  onMessage: (e: MessageEvent) => void
): () => void {
  if (
    !ws ||
    ws.readyState === WebSocket.CLOSING ||
    ws.readyState === WebSocket.CLOSED
  ) {
    connect(url);
  }
  listeners.add(onMessage);

  // ✅ cleanup은 void 반환
  return () => {
    listeners.delete(onMessage); // boolean을 무시하고 반환하지 않음
  };
}

export function sendWS(data: string) {
  if (ws?.readyState === WebSocket.OPEN) ws.send(data);
}
