#include <AccelStepper.h>

// ─────────────────────────────────────────────────────────────
// 인터럽트 핀
const int INTERRUPT_Z = 2;
const int INTERRUPT_X = 3;

// 스텝모터 핀
const int X_DIR_PIN  = 7;   
const int X_STEP_PIN = 8;
const int X_ENA_PIN  = 9;

const int Z_DIR_PIN  = 10;
const int Z_STEP_PIN = 11;
const int Z_ENA_PIN  = 12;

// 리니어 모터 드라이버 핀 (⚠ A0는 PWM 아님 → D5로 변경)
const int LINEAR_IN2 = 13;
const int LINEAR_ENA = 5;   // ★ PWM 가능 핀
const int LINEAR_IN1 = A1;

// 릴레이 핀
const int VACUUM_PUMP_PIN   = A2;
const int ELECTROMAGNET_PIN = A3;

// 버튼 핀
const int OPEN_BUTTON_PIN = A4;
const int SEAL_BUTTON_PIN = A5;

// ─────────────────────────────────────────────────────────────
// 상수 정의
// [좌표축] Z축 : 상단=0, 하단=양수(아래로 +) / X축 : 좌측=0, 우측=양수
const int MICROSTEP_MULTIPLIER = 16;

// Z축
const long Z_HOME_POSITION       = 0;        // 최상단을 Z축 원점으로
const long Z_REST_POSITION       = 420 * 16; // 대기 위치
const long Z_LID_ROOM_HEIGHT     = 420 * 16; // 뚜껑 집는 높이
const long Z_LID_INSERT_HEIGHT   = 430 * 16; // 와인 입구 삽입 높이
const long Z_LID_OPEN_HEIGHT     = 430 * 16; // 개봉 시 뚜껑 잡는 높이

// X축
const long X_HOME_POSITION         = 0;       // X축 원점
const long X_LID_ROOM_POSITION     = 165 * 16; // 뚜껑 보관소 위치
const long X_CAMERA_MAGNET_OFFSET  = 150 * 16; // 카메라-전자석 오프셋
const long X_CAMERA_VACUUM_OFFSET  = 200 * 16; // 카메라-진공관 오프셋
const long X_WINE_POSITION_APPROX  = 650 * 16;

// 속도 및 가속도
const float NORMAL_SPEED = 1000 * 16; // 기본 속도(steps/s)
const float NORMAL_ACCEL =  500 * 16; // 기본 가속도(steps/s^2)

const float OPEN_LID_SPEED = 25 * 16; // 개봉 시 Z축 속도
const float OPEN_LID_ACCEL = 15 * 16; // 개봉 시 Z축 가속도

// 리니어
const int LINEAR_MOTOR_SPEED = 250;          // 0~255 (PWM 듀티)
const unsigned long LINEAR_DURATION = 1000;  // ms

long g_finalWinePositionX = 0; // 개봉시 사용할 저장된 와인 위치

// 버튼 디바운스 변수
int sealButtonState = HIGH, lastSealButtonState = HIGH;
unsigned long lastSealDebounceTime = 0;

int openButtonState = HIGH, lastOpenButtonState = HIGH;
unsigned long lastOpenDebounceTime = 0;

const unsigned long debounceDelay = 50; // ms

// 시스템 상태 정의
enum SystemState { IDLE, SEALING, OPENING };
SystemState currentState = IDLE;

// 객체 생성
AccelStepper stepperZ(AccelStepper::DRIVER, Z_STEP_PIN, Z_DIR_PIN);
AccelStepper stepperX(AccelStepper::DRIVER, X_STEP_PIN, X_DIR_PIN);

// ─────────────────────────────────────────────────────────────
// 인터럽트 플래그/카운터 (디버깅용)
volatile bool zLimitHit = false;
volatile bool xLimitHit = false;
volatile unsigned long zIsrCount = 0;
volatile unsigned long xIsrCount = 0;

// ISR (⚠ Serial/Delay/Stepper 호출 금지)
void zLimitSwitchISR() {
  static unsigned long last = 0;
  unsigned long now = micros();
  if (now - last > 5000) { // ~5ms 디바운스
    zLimitHit = true;
    zIsrCount++;
    last = now;
  }
}

void xLimitSwitchISR() {
  static unsigned long last = 0;
  unsigned long now = micros();
  if (now - last > 5000) {
    xLimitHit = true;
    xIsrCount++;
    last = now;
  }
}

// ─────────────────────────────────────────────────────────────
// 기본 유틸
void motorsEnable() {
  Serial.println("모터 전원 활성화...");
  digitalWrite(Z_ENA_PIN, LOW);
  digitalWrite(X_ENA_PIN, LOW);
  delay(10);
}

void motorsDisable() {
  Serial.println("모터 전원 비활성화.");
  digitalWrite(Z_ENA_PIN, HIGH);
  digitalWrite(X_ENA_PIN, HIGH);
}

void control_Emagnet(bool power) {
  digitalWrite(ELECTROMAGNET_PIN, power ? HIGH : LOW);
  Serial.println(power ? "전자석 켜짐" : "전자석 꺼짐");
  delay(500);
}

void applyVacuum() {
  Serial.println("진공펌프 켜짐.");
  digitalWrite(VACUUM_PUMP_PIN, HIGH);
  delay(8000); // 진공 시간
  digitalWrite(VACUUM_PUMP_PIN, LOW);
  Serial.println("진공펌프 꺼짐.");
}

// ─────────────────────────────────────────────────────────────
// 안전 호밍: runSpeed + 타임아웃 + 백오프
bool moveZ_toHome() {
  zLimitHit = false;
  unsigned long t0 = millis();

  const float HOMING_SPEED = 800; // steps/s (장비에 맞게)
  stepperZ.setMaxSpeed(HOMING_SPEED);
  stepperZ.setSpeed(-HOMING_SPEED); // ★ 스위치 쪽 방향 (필요시 +로 바꾸세요)

  while (!zLimitHit) {
    stepperZ.runSpeed();
    if (millis() - t0 > 8000) {
      Serial.println("Z 홈 타임아웃");
      return false;
    }
  }

  // 트리거 지점에서 원점 설정
  stepperZ.setCurrentPosition(Z_HOME_POSITION);

  // 스위치에서 살짝 떼기(재트리거 방지) — 50 마이크로스텝 예시
  long backoff = 50L * MICROSTEP_MULTIPLIER;
  stepperZ.moveTo(Z_HOME_POSITION + backoff);
  stepperZ.runToPosition();

  Serial.print("Z축 홈 OK, ISR="); Serial.println(zIsrCount);
  return true;
}

bool moveX_toHome() {
  xLimitHit = false;
  unsigned long t0 = millis();

  const float HOMING_SPEED = 800;
  stepperX.setMaxSpeed(HOMING_SPEED);
  stepperX.setSpeed(-HOMING_SPEED); // ★ 스위치 쪽 방향 (필요시 +로 변경)

  while (!xLimitHit) {
    stepperX.runSpeed();
    if (millis() - t0 > 8000) {
      Serial.println("X 홈 타임아웃");
      return false;
    }
  }

  stepperX.setCurrentPosition(X_HOME_POSITION);

  long backoff = 50L * MICROSTEP_MULTIPLIER;
  stepperX.moveTo(X_HOME_POSITION + backoff);
  stepperX.runToPosition();

  Serial.print("X축 홈 OK, ISR="); Serial.println(xIsrCount);
  return true;
}

// ─────────────────────────────────────────────────────────────
// 초기화 루틴
void initialize() {
  Serial.println("\n--- 홈잉 프로세스 시작 ---");
  motorsEnable();

  bool zOk = moveZ_toHome();
  bool xOk = moveX_toHome();

  if (!zOk || !xOk) {
    Serial.println("홈잉 실패. 중단합니다.");
    motorsDisable();
    currentState = IDLE;
    return;
  }

  Serial.println("홈잉 완료. 초기 위치로 이동...");
  stepperZ.moveTo(Z_REST_POSITION);
  stepperZ.runToPosition();

  currentState = IDLE;
}

// ─────────────────────────────────────────────────────────────
// 카메라 정렬(X) : 시리얼 명령 'R','L','C'
bool cameraAlignX() {
  Serial.println("X축 정렬중... ('R':오른쪽, 'L':왼쪽, 'C':중앙)");
  unsigned long startTime = millis();
  while (millis() - startTime < 60000) {
    if (Serial.available() > 0) {
      char command = Serial.read();
      switch (command) {
        case 'R': stepperX.move( 10 * MICROSTEP_MULTIPLIER ); break;
        case 'L': stepperX.move(-10 * MICROSTEP_MULTIPLIER ); break;
        case 'C':
          stepperX.stop();
          Serial.println("  -> 카메라 정렬 완료.");
          return true;
      }
    }
    stepperX.run();
  }
  Serial.println("정렬 시간 초과!");
  return false;
}

// 리니어 모터 정렬 (PWM 제어)
void linearMotor_align() {
  Serial.println("와인병 정렬중...");
  digitalWrite(LINEAR_IN1, HIGH);
  digitalWrite(LINEAR_IN2, LOW);
  analogWrite(LINEAR_ENA, LINEAR_MOTOR_SPEED);
  delay(LINEAR_DURATION);

  digitalWrite(LINEAR_IN1, LOW);
  delay(500);

  digitalWrite(LINEAR_IN2, HIGH);
  delay(LINEAR_DURATION);

  digitalWrite(LINEAR_IN1, LOW);
  digitalWrite(LINEAR_IN2, LOW);
  analogWrite(LINEAR_ENA, 0);
  Serial.println("정렬 완료.");
}

// ─────────────────────────────────────────────────────────────
// 밀봉/개봉 프로세스
void sealProcess() {
  currentState = SEALING;
  motorsEnable();
  Serial.println("\n--- 밀봉 프로세스 시작 ---");

  // 1) Y축 정렬(리니어)
  Serial.println("단계 1: Y축 정렬...");
  linearMotor_align();

  // 2) 뚜껑 보관소로 이동
  Serial.println("단계 2: 뚜껑 보관소로 이동...");
  if (!moveZ_toHome()) { initialize(); goto SEAL_END; }
  stepperX.moveTo(X_LID_ROOM_POSITION);
  while (stepperX.distanceToGo() != 0) { stepperX.run(); }

  // 3) 뚜껑 획득
  Serial.println("단계 3: 뚜껑 획득...");
  stepperZ.moveTo(Z_LID_ROOM_HEIGHT);
  while (stepperZ.distanceToGo() != 0) { stepperZ.run(); }
  control_Emagnet(true);
  delay(300);
  if (!moveZ_toHome()) { initialize(); goto SEAL_END; }

  // 4) 와인 위치로 이동
  Serial.println("단계 4: 와인 위치로 이동...");
  stepperX.moveTo(X_WINE_POSITION_APPROX);
  while (stepperX.distanceToGo() != 0) { stepperX.run(); }

  // 카메라 정렬 신호
  Serial.println("A");
  Serial.flush();

  // 5) X축 카메라 정렬
  Serial.println("단계 5: X축 카메라 정렬...");
  if (cameraAlignX()) {
    g_finalWinePositionX = stepperX.currentPosition();
    Serial.print("와인 위치 저장: "); Serial.println(g_finalWinePositionX);

    // 6) 뚜껑 배치
    Serial.println("단계 6: 뚜껑 배치...");
    stepperX.moveTo(g_finalWinePositionX + X_CAMERA_MAGNET_OFFSET);
    while (stepperX.distanceToGo() != 0) { stepperX.run(); }
    stepperZ.moveTo(Z_LID_INSERT_HEIGHT);
    while (stepperZ.distanceToGo() != 0) { stepperZ.run(); }
    control_Emagnet(false);
    delay(500);

    // 7) 진공 준비
    Serial.println("단계 7: 진공 준비...");
    if (!moveZ_toHome()) { initialize(); goto SEAL_END; }

    stepperX.moveTo(g_finalWinePositionX - X_CAMERA_VACUUM_OFFSET);
    while (stepperX.distanceToGo() != 0) { stepperX.run(); }

    // 8) 진공 적용
    Serial.println("단계 8: 진공 적용...");
    stepperZ.moveTo(Z_LID_INSERT_HEIGHT);
    while (stepperZ.distanceToGo() != 0) { stepperZ.run(); }
    applyVacuum();

    // 9) 마무리
    Serial.println("단계 9: 마무리...");
    initialize();
    Serial.println("--- 밀봉 프로세스 완료 ---");
  } else {
    Serial.println("!!! 정렬 실패. 뚜껑 반환...");
    if (!moveZ_toHome()) { initialize(); goto SEAL_END; }
    stepperX.moveTo(X_LID_ROOM_POSITION);
    while (stepperX.distanceToGo() != 0) { stepperX.run(); }
    stepperZ.moveTo(Z_LID_ROOM_HEIGHT);
    while (stepperZ.distanceToGo() != 0) { stepperZ.run(); }
    control_Emagnet(false);
    if (!moveZ_toHome()) { /* ignore */ }
    initialize();
  }

SEAL_END:
  Serial.println("F");
  Serial.flush();
  motorsDisable();
  currentState = IDLE;
}

void openProcess() {
  // 저장된 위치 유효성
  if (g_finalWinePositionX < X_LID_ROOM_POSITION || g_finalWinePositionX > 1000L * 16) {
    Serial.println("ERROR: 유효하지 않은 와인 위치!");
    Serial.print("저장된 위치: "); Serial.println(g_finalWinePositionX);
    return;
  }

  currentState = OPENING;
  motorsEnable();
  Serial.println("\n--- 개봉 프로세스 시작 ---");

  // 1) 뚜껑으로 이동
  Serial.println("단계 1: 뚜껑으로 이동...");
  if (!moveZ_toHome()) { initialize(); goto OPEN_END; }
  stepperX.moveTo(g_finalWinePositionX + X_CAMERA_MAGNET_OFFSET);
  while (stepperX.distanceToGo() != 0) { stepperX.run(); }

  // 2) 뚜껑 접근 및 잡기
  Serial.println("단계 2: 뚜껑에 접근 및 잡기...");
  stepperZ.moveTo(Z_LID_OPEN_HEIGHT);
  while (stepperZ.distanceToGo() != 0) { stepperZ.run(); }
  control_Emagnet(true);
  delay(500);

  // 3) 진공 해제 대각선(현재 Z축만 왕복)
  Serial.println("단계 3: 진공 해제 대각선 왕복 운동...");
  float original_Z_Speed = stepperZ.maxSpeed();
  float original_Z_Accel = stepperZ.acceleration();
  stepperZ.setMaxSpeed(OPEN_LID_SPEED);
  stepperZ.setAcceleration(OPEN_LID_ACCEL);

  stepperZ.moveTo(430 * 16); while (stepperZ.distanceToGo() != 0) { stepperZ.run(); }
  stepperZ.moveTo(360 * 16); while (stepperZ.distanceToGo() != 0) { stepperZ.run(); }
  stepperZ.moveTo(410 * 16); while (stepperZ.distanceToGo() != 0) { stepperZ.run(); }
  stepperZ.moveTo(360 * 16); while (stepperZ.distanceToGo() != 0) { stepperZ.run(); }

  // 복원
  Serial.println("원래 속도로 복원...");
  stepperZ.setMaxSpeed(original_Z_Speed);
  stepperZ.setAcceleration(original_Z_Accel);

  // 4) 뚜껑 들어 올리기
  Serial.println("단계 4: 뚜껑 들어 올리기...");
  if (!moveZ_toHome()) { initialize(); goto OPEN_END; }

  // 5) 뚜껑 반환
  Serial.println("단계 5: 뚜껑을 보관소로 반환...");
  stepperX.moveTo(X_LID_ROOM_POSITION);
  while (stepperX.distanceToGo() != 0) { stepperX.run(); }
  stepperZ.moveTo(Z_LID_ROOM_HEIGHT);
  while (stepperZ.distanceToGo() != 0) { stepperZ.run(); }
  control_Emagnet(false);

  // 6) 마무리
  Serial.println("단계 6: Z축 상승 및 마무리...");
  initialize();
  Serial.println("--- 개봉 프로세스 완료 ---");

OPEN_END:
  Serial.println("F");
  Serial.flush();
  motorsDisable();
  currentState = IDLE;
}

// ─────────────────────────────────────────────────────────────
// 버튼 디바운스
bool isSealButtonPressed() {
  int reading = digitalRead(SEAL_BUTTON_PIN);
  if (reading != lastSealButtonState) {
    lastSealDebounceTime = millis();
  }
  if ((millis() - lastSealDebounceTime) > debounceDelay) {
    if (reading != sealButtonState) {
      sealButtonState = reading;
      if (sealButtonState == LOW) {
        lastSealButtonState = reading;
        Serial.println("밀봉 버튼 눌림.");
        return true;
      }
    }
  }
  lastSealButtonState = reading;
  return false;
}

bool isOpenButtonPressed() {
  int reading = digitalRead(OPEN_BUTTON_PIN);
  if (reading != lastOpenButtonState) {
    lastOpenDebounceTime = millis();
  }
  if ((millis() - lastOpenDebounceTime) > debounceDelay) {
    if (reading != openButtonState) {
      openButtonState = reading;
      if (openButtonState == LOW) {
        lastOpenButtonState = reading;
        Serial.println("개봉 버튼 눌림.");
        return true;
      }
    }
  }
  lastOpenButtonState = reading;
  return false;
}

// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);

  // 액츄에이터/릴레이/리니어
  pinMode(ELECTROMAGNET_PIN, OUTPUT);
  pinMode(VACUUM_PUMP_PIN, OUTPUT);
  pinMode(LINEAR_ENA, OUTPUT);
  pinMode(LINEAR_IN1, OUTPUT);
  pinMode(LINEAR_IN2, OUTPUT);

  // 버튼
  pinMode(SEAL_BUTTON_PIN, INPUT_PULLUP);
  pinMode(OPEN_BUTTON_PIN, INPUT_PULLUP);

  digitalWrite(ELECTROMAGNET_PIN, LOW);
  digitalWrite(VACUUM_PUMP_PIN, LOW);
  digitalWrite(LINEAR_IN1, LOW);
  digitalWrite(LINEAR_IN2, LOW);
  analogWrite(LINEAR_ENA, 0);

  // 리밋 스위치 (내장 풀업)
  pinMode(INTERRUPT_Z, INPUT_PULLUP);
  pinMode(INTERRUPT_X, INPUT_PULLUP);

  // 스텝모터 핀/ENA
  pinMode(X_DIR_PIN, OUTPUT);
  pinMode(X_STEP_PIN, OUTPUT);
  pinMode(X_ENA_PIN, OUTPUT);
  pinMode(Z_DIR_PIN, OUTPUT);
  pinMode(Z_STEP_PIN, OUTPUT);
  pinMode(Z_ENA_PIN, OUTPUT);
  digitalWrite(Z_ENA_PIN, HIGH);
  digitalWrite(X_ENA_PIN, HIGH);

  // 스텝모터 기본 파라미터
  stepperZ.setMaxSpeed(NORMAL_SPEED);
  stepperZ.setAcceleration(NORMAL_ACCEL);
  stepperX.setMaxSpeed(NORMAL_SPEED);
  stepperX.setAcceleration(NORMAL_ACCEL);
  motorsDisable();

  // 인터럽트 설정 (INPUT_PULLUP → 보통 HIGH, 눌리면 LOW → FALLING)
  attachInterrupt(digitalPinToInterrupt(INTERRUPT_Z), zLimitSwitchISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(INTERRUPT_X), xLimitSwitchISR, FALLING);

  Serial.println("시스템 준비 완료. 버튼 입력 대기중.");
}

void loop() {
  switch (currentState) {
    case IDLE:
      if (isSealButtonPressed()) {
        Serial.println("1");
        Serial.flush();
        sealProcess();
      }
      if (isOpenButtonPressed()) {
        Serial.println("2");
        Serial.flush();
        openProcess();
      }
      break;

    case SEALING:
    case OPENING:
      // 프로세스 함수가 모든 것을 처리
      break;
  }
}
