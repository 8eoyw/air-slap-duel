SRC := ../firmware/libraries/SlapCommon/src
CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -I$(SRC)

test: ../build/test_referee
	../build/test_referee

../build/test_referee: test_referee.cpp $(SRC)/referee_core.cpp $(SRC)/referee_core.h $(SRC)/slap_protocol.h $(SRC)/slap_game_config.h
	mkdir -p ../build
	$(CXX) $(CXXFLAGS) test_referee.cpp $(SRC)/referee_core.cpp -o $@

.PHONY: test sim

# Simulated match for tools/dashboard.py (no hardware): ../build/sim_match --realtime | python ../tools/dashboard.py --stdin
sim: ../build/sim_match

../build/sim_match: ../tools/sim_match.cpp $(SRC)/referee_core.cpp $(SRC)/referee_core.h $(SRC)/slap_protocol.h $(SRC)/slap_game_config.h
	mkdir -p ../build
	$(CXX) $(CXXFLAGS) -O2 ../tools/sim_match.cpp $(SRC)/referee_core.cpp -o $@
