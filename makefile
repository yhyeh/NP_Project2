all: np_simple np_single_proc
np_simple: np_simple.cpp
	g++ -o np_simple np_simple.cpp

np_single_proc: np_single_proc.cpp
	g++ -o np_single_proc np_single_proc.cpp

.PHONY: clean
clean:
	rm -f np_simple np_single_proc
