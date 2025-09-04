import { useEffect } from "react";
import { useNavigate } from "react-router-dom";
import styles from "../styles/MainPage.module.css";
import Icon from "../assets/Icon.svg";

const Splash = () => {
  const navigate = useNavigate();

  useEffect(() => {
    const timeout = setTimeout(() => {
      navigate("/main");
    }, 5000);

    return () => clearTimeout(timeout);
  }, [navigate]);

  return (
    <div className={`${styles.splash}`}>
      <img src={Icon} alt="아이콘" />
    </div>
  );
};

export default Splash;
