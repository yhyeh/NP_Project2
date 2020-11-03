np_simple: np_simple.cpp
	g++ -o np_simple np_simple.cpp

.PHONY: clean
clean:
	rm -f np_simple
