#pragma once
#include <stdbool.h>
#include <windows.h>

// Cross Application Mutex used to avoid multiple calls to sentinel at the same time - raii
class CrossAppMutex final
{
public:
  // constructor
  CrossAppMutex(const CString& name) :
    m_mutex(),
    m_name(name),
    m_owned(false)
  {
    LockMutex();
  }

  // destructor
  ~CrossAppMutex()
  {
    UnlockMutex();
  }

private:
  // take the ownership of the mutex
  void LockMutex() noexcept
  {
    m_mutex = CreateMutex(nullptr, true,  m_name);
    if (m_mutex)
    {
      const DWORD duration = WaitForSingleObject(m_mutex, INFINITE);
      if ((duration != WAIT_ABANDONED) && 
          (duration != WAIT_TIMEOUT) && 
          (duration != WAIT_FAILED))
        m_owned = true;
    }
  }

  // release the ownership of the mutex
  void UnlockMutex() noexcept
  {
    // release mutex
    if (m_mutex)
    {
      if (m_owned)
        ReleaseMutex(m_mutex);
      CloseHandle(m_mutex);
    }
  }

private:
  // cross-application mutex
  HANDLE m_mutex;
  CString m_name;
  bool m_owned;
};