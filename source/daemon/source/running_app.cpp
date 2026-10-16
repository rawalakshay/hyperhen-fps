/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Running BigApp query. Separate from daemon_utils.cpp so the standalone FPS
 * monitor can link the FPS sampler without the launcher.
 */

#include "daemon_ops.hpp"

#include <string.h>

extern "C" {
  int sceSystemServiceGetAppIdOfRunningBigApp();
  int sceSystemServiceGetAppTitleId(int app_id, char *title_id);
}

bool Get_Running_App_TID(std::string &title_id, int &BigAppid) {
  char tid[255];
  BigAppid = sceSystemServiceGetAppIdOfRunningBigApp();
  if (BigAppid < 0)
    return false;

  (void)memset(tid, 0, sizeof tid);
  if (sceSystemServiceGetAppTitleId(BigAppid, &tid[0]) != 0)
    return false;

  title_id = std::string(tid);
  return true;
}
