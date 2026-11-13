#include "StarSignalHandler.hpp"

#include <signal.h>
#include <exception>
#include <stdlib.h>
#include "StarException.hpp"

namespace Star {

struct SignalHandlerImpl {
  bool handlingFatal;
  bool handlingInterrupt;
  bool interrupted;

  SignalHandlerImpl() : handlingFatal(false), handlingInterrupt(false), interrupted(false) {}

  ~SignalHandlerImpl() {
    setHandleFatal(false);
    setHandleInterrupt(false);
  }

  void setHandleFatal(bool b) {
    handlingFatal = b;
    if (handlingFatal) {
#ifdef STAR_SYSTEM_ANDROID
      // Android gives no console, so make every way of dying leave a reason
      // in the log: aborts, uncaught exceptions and stack overflows too.
      stack_t altStack;
      altStack.ss_size = 64 * 1024;
      altStack.ss_sp = malloc(altStack.ss_size);
      altStack.ss_flags = 0;
      sigaltstack(&altStack, nullptr);
      struct sigaction action;
      sigemptyset(&action.sa_mask);
      action.sa_handler = handleFatal;
      action.sa_flags = SA_ONSTACK;
      sigaction(SIGSEGV, &action, nullptr);
      sigaction(SIGABRT, &action, nullptr);
      sigaction(SIGILL, &action, nullptr);
      sigaction(SIGFPE, &action, nullptr);
      sigaction(SIGBUS, &action, nullptr);
      std::set_terminate([]() {
        if (auto current = std::current_exception()) {
          try {
            std::rethrow_exception(current);
          } catch (std::exception const& e) {
            fatalException(e, true);
          } catch (...) {
            fatalError("Unknown exception terminated the game", true);
          }
        }
        fatalError("std::terminate called", true);
      });
      return;
#endif
      signal(SIGSEGV, handleFatal);
      signal(SIGILL, handleFatal);
      signal(SIGFPE, handleFatal);
      signal(SIGBUS, handleFatal);
    } else {
      signal(SIGSEGV, SIG_DFL);
      signal(SIGILL, SIG_DFL);
      signal(SIGFPE, SIG_DFL);
      signal(SIGBUS, SIG_DFL);
    }
  }

  void setHandleInterrupt(bool b) {
    handlingInterrupt = b;
    if (handlingInterrupt)
      signal(SIGINT, handleInterrupt);
    else
      signal(SIGINT, SIG_DFL);
  }

  static void handleFatal(int signum) {
    static volatile sig_atomic_t handling = 0;
    if (handling) {
      // fatalError aborts; let that second signal kill the process
      signal(signum, SIG_DFL);
      raise(signum);
      return;
    }
    handling = 1;
    if (signum == SIGABRT)
      fatalError("Abort Encountered!", true);
    if (signum == SIGSEGV)
      fatalError("Segfault Encountered!", true);
    else if (signum == SIGILL)
      fatalError("Illegal Instruction Encountered!", true);
    else if (signum == SIGFPE)
      fatalError("Floating Point Exception Encountered!", true);
    else if (signum == SIGBUS)
      fatalError("Bus Error Encountered!", true);
  }

  static void handleInterrupt(int) {
    if (SignalHandler::s_singleton)
      SignalHandler::s_singleton->interrupted = true;
  }
};

SignalHandlerImplUPtr SignalHandler::s_singleton;

SignalHandler::SignalHandler() {
  if (s_singleton)
    throw StarException("Singleton SignalHandler has been constructed twice!");

  s_singleton = make_unique<SignalHandlerImpl>();
}

SignalHandler::~SignalHandler() {
  s_singleton.reset();
}

void SignalHandler::setHandleFatal(bool handleFatal) {
  s_singleton->setHandleFatal(handleFatal);
}

bool SignalHandler::handlingFatal() const {
  return s_singleton->handlingFatal;
}

void SignalHandler::setHandleInterrupt(bool handleInterrupt) {
  s_singleton->setHandleInterrupt(handleInterrupt);
}

bool SignalHandler::handlingInterrupt() const {
  return s_singleton->handlingInterrupt;
}

bool SignalHandler::interruptCaught() const {
  return s_singleton->interrupted;
}

}
