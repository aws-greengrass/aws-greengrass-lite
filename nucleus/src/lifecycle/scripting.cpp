#include "scripting.hpp"

bool scriptRunner::willRun() {
    return false;
}

bool scriptRunner::start() {
    std::cout << "script starting" << std::endl;
    return true;
}

void scriptRunner::kill() {
}
