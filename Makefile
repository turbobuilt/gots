CXX = g++
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra -pthread
LDFLAGS = -pthread

SRCDIR = .
SOURCES = compiler.cpp lexer.cpp parser.cpp type_inference.cpp x86_codegen.cpp wasm_codegen.cpp ast_codegen.cpp runtime.cpp runtime_syscalls.cpp lexical_scope.cpp regex.cpp error_reporter.cpp syntax_highlighter.cpp simple_main.cpp
OBJECTS = $(SOURCES:.cpp=.o)
TARGET = gots

.PHONY: all clean debug

all: $(TARGET)

debug: CXXFLAGS += -g -DDEBUG
debug: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

run: $(TARGET)
	./$(TARGET)

test: $(TARGET)
	./$(TARGET)

install: $(TARGET)
	cp $(TARGET) /usr/local/bin/

.PHONY: x86-test wasm-test

x86-test: $(TARGET)
	@echo "Testing x86-64 backend..."
	./$(TARGET)

wasm-test: $(TARGET)
	@echo "Testing WebAssembly backend..."
	./$(TARGET)

benchmark: $(TARGET)
	@echo "Running performance benchmarks..."
	time ./$(TARGET)

# Dependencies
compiler.o: compiler.h runtime.h
lexer.o: compiler.h
parser.o: compiler.h
type_inference.o: compiler.h
x86_codegen.o: compiler.h
wasm_codegen.o: compiler.h
ast_codegen.o: compiler.h runtime_object.h
runtime.o: runtime.h lexical_scope.h
runtime_syscalls.o: runtime_syscalls.h runtime.h runtime_object.h
lexical_scope.o: lexical_scope.h compiler.h
regex.o: regex.h runtime.h
error_reporter.o: compiler.h
syntax_highlighter.o: compiler.h
main.o: compiler.h tensor.h promise.h runtime.h