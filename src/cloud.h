#pragma once
// ==================================================================
//  cloud.h — v0.13: облачная оболочка (каталог, NTP, run-app).
// ==================================================================

// каталог
bool cloudCatalogOk();
int  cloudCount();
const char* cloudTitle(int i);
const char* cloudFile(int i);
const char* cloudCat(int i);

// синхронизации (boot / по требованию)
bool cloudEnsureWifi(bool showScreen);
bool cloudFetchCatalog(bool showScreen);
bool cloudSyncTime(bool showScreen);

// download -> xlaRun -> delete
bool cloudRunApp(int idx);
