#pragma once

// Common path
#define ROOT										"/root/trunk/faceRecognizer_Doorlock/"
#define ASSERT									ROOT "assert/"


// LBPH related paths
#define DETECTOR_PATH									ASSERT "detector/"
#define FACEDETECTOR									DETECTOR_PATH "haarcascade_frontalface_default.xml"
#define EYESDETECTOR                  DETECTOR_PATH "haarcascade_eye.xml"

#define LPBH_MODEL_PATH							ASSERT "lbph_model/"
#define LPBH_MODEL									"face_model.yml"

#define LBPH_MODEL_FILE								ASSERT	"face_model.yml"
#define USER_FACES_DIR								ASSERT "face_images/"


// Embedding related paths
#define MOBILEFACE_PATH							ASSERT "mobileFaceNet/"
#define MOBILEFACE_MODEL						"mobileface.onnx"	
#define EMBEDDING_JSON_PATH					ASSERT "embedding/"
#define EMBEDDING_JSON							"embeddings.json"


// Images
#define IMAGES_PATH									ASSERT "images/"
#define OPEN_IMAGE									"open_image.PNG"
#define STANDBY_IMAGE								"standby.png" 

