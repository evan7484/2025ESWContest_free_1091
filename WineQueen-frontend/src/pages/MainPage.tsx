import { useNavigate } from "react-router-dom";
import { useEffect, useRef, useState, useCallback } from "react";
import styles from "../styles/MainPage.module.css";
import Wine_1 from "../assets/Wine_1.svg";
import { getWebSocketUrl } from "../constants/constants";
import { subscribeWS } from "../lib/ws";

const formatNow = (d: Date) => {
  const y = d.getFullYear();
  const m = String(d.getMonth() + 1).padStart(2, "0");
  const day = String(d.getDate()).padStart(2, "0");
  const weekday = d.toLocaleDateString("en-US", { weekday: "short" }); // ex) Mon / 월
  const hh = String(d.getHours()).padStart(2, "0");
  const mm = String(d.getMinutes()).padStart(2, "0");
  const ss = String(d.getSeconds()).padStart(2, "0");
  return `${y}/${m}/${day} (${weekday}) ${hh}:${mm}:${ss}`;
};

const formatDate = (dateStr: string | null) => {
  if (!dateStr) return "No record of sealing";
  const date = new Date(dateStr);
  return isNaN(date.getTime())
    ? "No record of sealing"
    : `${date.getFullYear()}/${
        date.getMonth() + 1
      }/${date.getDate()} , ${date.getHours()}:${date.getMinutes()}`;
};

const MainPage = () => {
  const navigate = useNavigate();
  const [currentTime, setCurrentTime] = useState(() => new Date());
  const [startTime, setStartTime] = useState<string | null>(null);
  const [focusedIndex, setFocusedIndex] = useState(0);
  const buttonRefs = useRef<(HTMLButtonElement | null)[]>([]);

  useEffect(() => {
    const id = window.setInterval(() => setCurrentTime(new Date()), 1000);
    return () => window.clearInterval(id);
  }, []);

  const handleWSMessage = useCallback((e: MessageEvent) => {
    let d: unknown = e.data;

    // 문자열 프레임(하트비트/레거시) 처리
    if (typeof d === "string") {
      const t = d.trim?.();
      if (t === "ping" || t === "pong") return;
      try {
        d = JSON.parse(t);
      } catch {
        const n = Number(t);
        if (!Number.isNaN(n)) {
          const idx = n - 1;
          if (idx >= 0) buttonRefs.current[idx]?.click?.();
        }
        return;
      }
    }

    if (!d || typeof d !== "object") return;
    const msg = d as any;

    switch (msg.type) {
      case "button": {
        const idx = Number(msg.value) - 1;
        if (idx >= 0) buttonRefs.current[idx]?.click?.();
        break;
      }
      case "redirect": {
        // 서버가 "/seal" | "/open" 을 보냄 → 그대로 이동
        const page = String(msg.page || "");
        if (page === "/seal") {
          const now = new Date().toISOString();
          setStartTime(now);
          setStorage(now);
          navigate(page);
        } else if (page === "/open") {
          handleReset();
          navigate(page);
        }
        break;
      }
      default:
        break;
    }
  }, [navigate]);

  useEffect(() => {
    const url = getWebSocketUrl("/ws");
    // A) 원본 그대로 보기 (항상 출력)
    const offRaw = subscribeWS(url, (e) => {
      const d = e.data;
      // 메시지 타입/길이까지 같이 출력
      if (typeof d === "string") {
        console.debug("[WS RAW text]", d);
      } else if (d instanceof Blob) {
        console.debug("[WS RAW blob]", d);
      } else if (d instanceof ArrayBuffer) {
        console.debug("[WS RAW buffer]", new Uint8Array(d));
      } else {
        console.debug("[WS RAW]", d);
      }
    });
    const off = subscribeWS(url, handleWSMessage);
    return () => {
      off();
      offRaw();
    };
  }, [handleWSMessage]);

  const setStorage = (value: string | null) => {
    if (value) localStorage.setItem("startTime", value);
    else localStorage.removeItem("startTime");
  };

  const handleOpen = () => {
    navigate("/open");
    handleReset();
  };

  const handleClose = () => {
    const now = new Date().toISOString();
    setStartTime(now);
    setStorage(now);
    navigate("/seal");
  };

  const handleReset = () => {
    setStartTime(null);
    setStorage(null);
  };

  const buttonActions = [handleClose, handleOpen, handleReset];

  // 초기값 로드
  useEffect(() => {
    setStartTime(localStorage.getItem("startTime"));
  }, []);

  // 키보드 네비게이션
  const handleKeyDown = useCallback(
    (e: KeyboardEvent) => {
      if (e.key === "ArrowRight") {
        setFocusedIndex((prev) => (prev + 1) % buttonActions.length);
      } else if (e.key === "ArrowLeft") {
        setFocusedIndex(
          (prev) => (prev - 1 + buttonActions.length) % buttonActions.length
        );
      } else if (e.key === "Enter") {
        buttonActions[focusedIndex]?.();
      }
    },
    [focusedIndex, buttonActions]
  );

  useEffect(() => {
    window.addEventListener("keydown", handleKeyDown);
    return () => window.removeEventListener("keydown", handleKeyDown);
  }, [handleKeyDown]);

  useEffect(() => {
    buttonRefs.current[focusedIndex]?.focus();
  }, [focusedIndex]);

  return (
    <div className={styles.containerWrapper}>
      <div className={styles.topbar} role="timer" aria-live="polite">
        {formatNow(currentTime)}
      </div>
      <div className={styles.header}>
        <div>
          <span style={{ color: "#FFF" }}>Please select </span>
          <span style={{ color: "#FFF" }}>the </span>
        </div>
        <div>
          <span style={{ color: "#FFDB58" }}> Seal </span>
          <span style={{ color: "#FFF" }}>or</span>
          <span style={{ color: "#FFDB58" }}> Open </span>
          <span style={{ color: "#2C001E" }}>Wine </span>
          <span style={{ color: "#FFF" }}>button.</span>
        </div>
      </div>

      <div className={styles.wrapper}>
        <div className={styles.section}>
          <div className={styles.button_wrapper}>
            <button
              ref={(el) => {
                buttonRefs.current[0] = el;
              }}
              className={styles.button}
              onClick={handleClose}
            >
              Seal
            </button>
            <button
              ref={(el) => {
                buttonRefs.current[1] = el;
              }}
              className={styles.button}
              onClick={handleOpen}
            >
              Open
            </button>
          </div>

          <div className={styles.rectangle}>
            <img src={Wine_1} alt="와인1" />
          </div>

          <div className={styles.data}>
            {startTime && <div>Last sealed date</div>}
            <div>{formatDate(startTime)}</div>
          </div>
        </div>
      </div>
    </div>
  );
};

export default MainPage;
