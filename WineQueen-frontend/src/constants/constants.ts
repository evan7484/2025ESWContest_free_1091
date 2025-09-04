// 바뀌는 IP와 포트 한 곳에서 관리
export const NetWorkIp = "192.168.0.244:8000"; // 매번 여기만 수정

// WebSocket URL 생성 (HTTPS → WSS, HTTP → WS)
export const getWebSocketUrl = (path: string) => {
  const protocol = window.location.protocol === "https:" ? "wss://" : "ws://";
  return `${protocol}${NetWorkIp}${path}`;
};

// HTTP URL 생성 (video_feed 같은 스트림용)
export const getHttpUrl = (path: string) => {
  const protocol =
    window.location.protocol === "https:" ? "https://" : "http://";
  return `${protocol}${NetWorkIp}${path}`;
};
