#include "preferences.h"
#include <Dialogs.h>
#include <Files.h>
#include <Folders.h>
#include <MacMemory.h>
#include <TextUtils.h>
#include <iostream>
#include <sstream>
#include <stdlib.h>
#include <string.h>

namespace mochila {

std::string PreferencesManager::getPreferencesPath() {
  // Get the Preferences folder in the System Folder
  FSSpec prefSpec;
  OSErr err = FindFolder(kOnSystemDisk, kPreferencesFolderType, kCreateFolder,
                         &prefSpec.vRefNum, &prefSpec.parID);
  if (err != noErr) {
    std::cerr << "[Prefs] FindFolder failed: " << err << std::endl;
    return "";
  }

  // Create FSSpec for our preferences file
  Str255 fileName = "\pMochila Preferences";
  err = FSMakeFSSpec(prefSpec.vRefNum, prefSpec.parID, fileName, &prefSpec);

  // Convert FSSpec to path string (simplified - just return a marker)
  // We'll use the FSSpec directly in load/save
  return "Mochila Preferences";
}

bool PreferencesManager::load(MochilaPreferences &prefs) {
  // Get preferences folder
  FSSpec prefSpec;
  short vRefNum;
  long dirID;

  OSErr err = FindFolder(kOnSystemDisk, kPreferencesFolderType, kCreateFolder,
                         &vRefNum, &dirID);
  if (err != noErr) {
    std::cout << "[Prefs] Could not find Preferences folder" << std::endl;
    return false;
  }

  // Create FSSpec for our file
  Str255 fileName = "\pMochila Preferences";
  err = FSMakeFSSpec(vRefNum, dirID, fileName, &prefSpec);
  if (err == fnfErr) {
    std::cout << "[Prefs] Preferences file not found (first launch)"
              << std::endl;
    return false;
  }
  if (err != noErr) {
    std::cerr << "[Prefs] FSMakeFSSpec error: " << err << std::endl;
    return false;
  }

  // Open the file
  short refNum;
  err = FSpOpenDF(&prefSpec, fsRdPerm, &refNum);
  if (err != noErr) {
    std::cerr << "[Prefs] Could not open preferences file: " << err
              << std::endl;
    return false;
  }

  // Read file contents
  long fileSize;
  err = GetEOF(refNum, &fileSize);
  if (err != noErr || fileSize > 4096) { // Sanity check
    FSClose(refNum);
    return false;
  }

  char *buffer = new char[fileSize + 1];
  long count = fileSize;
  err = FSRead(refNum, &count, buffer);
  FSClose(refNum);

  if (err != noErr) {
    delete[] buffer;
    return false;
  }

  buffer[fileSize] = '\0';
  std::string content(buffer);
  delete[] buffer;

  // Parse simple key=value format
  std::istringstream stream(content);
  std::string line;

  while (std::getline(stream, line)) {
    size_t pos = line.find('=');
    if (pos == std::string::npos)
      continue;

    std::string key = line.substr(0, pos);
    std::string value = line.substr(pos + 1);

    if (key == "server_host") {
      prefs.serverHost = value;
    } else if (key == "server_port") {
      prefs.serverPort = atoi(value.c_str());
    } else if (key == "last_url") {
      prefs.lastUrl = value;
    }
  }

  std::cout << "[Prefs] Loaded: " << prefs.serverHost << ":" << prefs.serverPort
            << std::endl;
  return true;
}

bool PreferencesManager::save(const MochilaPreferences &prefs) {
  // Get preferences folder
  FSSpec prefSpec;
  short vRefNum;
  long dirID;

  OSErr err = FindFolder(kOnSystemDisk, kPreferencesFolderType, kCreateFolder,
                         &vRefNum, &dirID);
  if (err != noErr) {
    std::cerr << "[Prefs] Could not find Preferences folder" << std::endl;
    return false;
  }

  // Create FSSpec for our file
  Str255 fileName = "\pMochila Preferences";
  err = FSMakeFSSpec(vRefNum, dirID, fileName, &prefSpec);

  // Delete existing file if it exists
  if (err == noErr) {
    FSpDelete(&prefSpec);
  }

  // Create new file
  err = FSpCreate(&prefSpec, 'MOCH', 'TEXT', smSystemScript);
  if (err != noErr) {
    std::cerr << "[Prefs] Could not create preferences file: " << err
              << std::endl;
    return false;
  }

  // Open for writing
  short refNum;
  err = FSpOpenDF(&prefSpec, fsWrPerm, &refNum);
  if (err != noErr) {
    std::cerr << "[Prefs] Could not open for writing: " << err << std::endl;
    return false;
  }

  // Build preferences content
  std::ostringstream content;
  content << "server_host=" << prefs.serverHost << "\n";
  content << "server_port=" << prefs.serverPort << "\n";
  content << "last_url=" << prefs.lastUrl << "\n";

  std::string data = content.str();
  long count = data.length();
  err = FSWrite(refNum, &count, data.c_str());
  FSClose(refNum);

  if (err != noErr) {
    std::cerr << "[Prefs] Write failed: " << err << std::endl;
    return false;
  }

  std::cout << "[Prefs] Saved preferences" << std::endl;
  return true;
}

bool PreferencesManager::showConfigDialog(MochilaPreferences &prefs) {
  // For now, use a simple text-based prompt via stdout
  // TODO: Implement proper Mac OS 9 dialog with Dialog Manager

  std::cout << "\n========================================" << std::endl;
  std::cout << "  Mochila Server Configuration" << std::endl;
  std::cout << "========================================" << std::endl;
  std::cout << "\nNo saved preferences found." << std::endl;
  std::cout << "Please configure your Mochila server:\n" << std::endl;

  std::cout << "Server Address (e.g., 192.168.1.100): ";
  std::getline(std::cin, prefs.serverHost);

  if (prefs.serverHost.empty()) {
    std::cout << "Configuration cancelled." << std::endl;
    return false;
  }

  std::string portStr;
  std::cout << "Server Port (default 8080): ";
  std::getline(std::cin, portStr);

  if (!portStr.empty()) {
    prefs.serverPort = atoi(portStr.c_str());
  } else {
    prefs.serverPort = 8080;
  }

  std::cout << "\nConfiguration saved!" << std::endl;
  std::cout << "Server: " << prefs.serverHost << ":" << prefs.serverPort
            << std::endl;
  std::cout << "========================================\n" << std::endl;

  return true;
}

} // namespace mochila
