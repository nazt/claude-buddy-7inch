#pragma once
#include <Preferences.h>

static Preferences _prefs;

static void persistLoad(uint32_t* approvals, uint32_t* denials,
                        char* owner, size_t ownerSz,
                        char* pet, size_t petSz) {
  _prefs.begin("buddy", true);
  *approvals = _prefs.getUInt("appr", 0);
  *denials = _prefs.getUInt("deny", 0);
  String o = _prefs.getString("owner", "");
  String p = _prefs.getString("pet", "buddy");
  _prefs.end();
  strncpy(owner, o.c_str(), ownerSz - 1); owner[ownerSz - 1] = 0;
  strncpy(pet, p.c_str(), petSz - 1); pet[petSz - 1] = 0;
}

static void persistSaveStats(uint32_t approvals, uint32_t denials) {
  _prefs.begin("buddy", false);
  _prefs.putUInt("appr", approvals);
  _prefs.putUInt("deny", denials);
  _prefs.end();
}

static void persistSaveIdentity(const char* owner, const char* pet) {
  _prefs.begin("buddy", false);
  _prefs.putString("owner", owner);
  _prefs.putString("pet", pet);
  _prefs.end();
}
