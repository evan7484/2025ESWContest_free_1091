from ultralytics import YOLO

print("Loading the YOLO model...")
# 기존에 학습시킨 .pt 모델 파일을 로드합니다.
model = YOLO('best_wCrop.pt')

print("Starting model export to TFLite format...")
# TFLite 형식으로 모델을 변환합니다. 
# imgsz는 main.py에서 사용하던 크기와 동일하게 192로 설정합니다.
model.export(format='tflite', imgsz=192)

print("Export complete!")
print("A new file like 'best_wCrop_float32.tflite' should now be in your directory.")
