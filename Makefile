# 컴파일러 및 플래그 설정
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
LDFLAGS = -lsqlite3

# 디렉토리 설정
SRC_DIR = .
MODULE_DIR = module
OBJ_DIR = build

# 실행 파일 이름
TARGET = graph_viewer

# 소스 파일들
MAIN_SRC = $(SRC_DIR)/main.cpp
MODULE_SRCS = $(MODULE_DIR)/rdf_graph_generator.cpp \
              $(MODULE_DIR)/line_finder_v2.cpp \
              $(MODULE_DIR)/log_util.cpp

# 오브젝트 파일들
MAIN_OBJ = $(OBJ_DIR)/main.o
MODULE_OBJS = $(OBJ_DIR)/rdf_graph_generator.o \
              $(OBJ_DIR)/line_finder_v2.o \
              $(OBJ_DIR)/log_util.o

ALL_OBJS = $(MAIN_OBJ) $(MODULE_OBJS)

# 기본 타겟
all: $(OBJ_DIR) $(TARGET)

# 오브젝트 디렉토리 생성
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# 실행 파일 빌드
$(TARGET): $(ALL_OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(ALL_OBJS) $(LDFLAGS)
	@echo "빌드 완료: $(TARGET)"

# 메인 오브젝트 파일
$(OBJ_DIR)/main.o: $(MAIN_SRC) \
                   $(MODULE_DIR)/rdf_graph_generator.hpp \
                   $(MODULE_DIR)/line_finder_v2.hpp
	$(CXX) $(CXXFLAGS) -c $(MAIN_SRC) -o $(OBJ_DIR)/main.o

# 모듈 오브젝트 파일들
$(OBJ_DIR)/rdf_graph_generator.o: $(MODULE_DIR)/rdf_graph_generator.cpp $(MODULE_DIR)/rdf_graph_generator.hpp
	$(CXX) $(CXXFLAGS) -c $(MODULE_DIR)/rdf_graph_generator.cpp -o $(OBJ_DIR)/rdf_graph_generator.o

$(OBJ_DIR)/line_finder_v2.o: $(MODULE_DIR)/line_finder_v2.cpp $(MODULE_DIR)/line_finder_v2.hpp $(MODULE_DIR)/rdf_graph_generator.hpp
	$(CXX) $(CXXFLAGS) -c $(MODULE_DIR)/line_finder_v2.cpp -o $(OBJ_DIR)/line_finder_v2.o

$(OBJ_DIR)/log_util.o: $(MODULE_DIR)/log_util.cpp $(MODULE_DIR)/log_util.hpp
	$(CXX) $(CXXFLAGS) -c $(MODULE_DIR)/log_util.cpp -o $(OBJ_DIR)/log_util.o

# 정리 (오브젝트 파일 및 실행 파일 삭제)
clean:
	rm -rf $(OBJ_DIR) $(TARGET)
	@echo "정리 완료"

# 재빌드 (clean 후 빌드)
rebuild: clean all

# 도움말
help:
	@echo "사용 가능한 타겟:"
	@echo "  make        - 프로젝트 빌드"
	@echo "  make clean  - 빌드 파일 정리"
	@echo "  make rebuild - 재빌드 (clean + build)"
	@echo "  make help   - 이 도움말 표시"

.PHONY: all clean rebuild help

