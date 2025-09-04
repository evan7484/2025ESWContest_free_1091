import { useNavigate } from "react-router-dom";
import { useEffect, useRef, useCallback } from "react";
import styles from "../styles/Wine.module.css";
import { getWebSocketUrl, getHttpUrl } from "../constants/constants";
import { subscribeWS } from "../lib/ws";

const OpenWine = () => {
  const navigate = useNavigate();
  const firedRef = useRef(false);
  const timerRef = useRef<number | null>(null);

  const onClick = useCallback(() => {
    navigate("/main");
  }, [navigate]);

  const fireOnce = useCallback(() => {
    if (firedRef.current) return;
    firedRef.current = true;
    if (timerRef.current) {
      clearTimeout(timerRef.current);
      timerRef.current = null;
    }
    onClick();
  }, [onClick]);

  useEffect(() => {
    timerRef.current = window.setTimeout(() => {
      fireOnce();
    }, 30_000);

    return () => {
      if (timerRef.current) clearTimeout(timerRef.current);
      timerRef.current = null;
    };
  }, [fireOnce]);

  useEffect(() => {
    const off = subscribeWS(getWebSocketUrl("/ws"), (e) => {
      let data: any = e.data;

      if (typeof data === "string") {
        const t = data.trim?.();
        if (t === "3") {
          fireOnce();
          return;
        }
        if (t === "ping" || t === "pong") return;
        try {
          data = JSON.parse(t);
        } catch {
          return;
        }
      }

      if (data?.type === "button" && Number(data.value) === 3) {
        fireOnce();
      }
    });

    return () => off();
  }, [fireOnce]);

  useEffect(() => {
    const h = (e: KeyboardEvent) => {
      if (e.key === "3" || e.code === "Digit3" || e.code === "Numpad3") {
        fireOnce();
      }
    };
    window.addEventListener("keydown", h);
    return () => window.removeEventListener("keydown", h);
  }, [fireOnce]);

  useEffect(() => {
    fetch(getHttpUrl("/control/open"), { method: "POST" }).catch(() => {});
    return () => {
      fetch(getHttpUrl("/control/stop"), { method: "POST" }).catch(() => {});
    };
  }, []);

  return (
    <div className={styles.wrapper}>
      <div className={styles.header}>Open the Wine</div>
      <div className={styles.section}>
        <div className={styles.rectangle}>
          <img
            src={getHttpUrl("/video_feed")}
            alt="Yolo Stream"
            className={styles.rectangle_img}
            crossOrigin="anonymous"
          />
        </div>
      </div>
    </div>
  );
};

export default OpenWine;
