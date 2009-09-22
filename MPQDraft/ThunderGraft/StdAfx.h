// stdafx.h : include file for standard system include files,
//  or project specific include files that are used frequently, but
//      are changed infrequently
//

#if !defined(AFX_STDAFX_H__D3D148B1_5FE6_4FED_9739_C64B3C1BE23B__INCLUDED_)
#define AFX_STDAFX_H__D3D148B1_5FE6_4FED_9739_C64B3C1BE23B__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#define _CRT_SECURE_NO_WARNINGS

// Windows includes
#include <windows.h>
#include <shlwapi.h>
#include <rpc.h>	// Needed for UUIDToString
#include <psapi.h> // Needed for GetModuleInformation
#include <dsound.h>

// CRT and STL includes
#include <DelayImp.h>
#include <process.h>
#include <assert.h>
#include <math.h>

#include <list>
#include <hash_map>

using namespace std;

// Other includes
#include <QDebug.h>
#include <QHookAPI.h>
#include <MPQDraftPlugin.h>
#include <fmod.hpp>

#endif // !defined(AFX_STDAFX_H__D3D148B1_5FE6_4FED_9739_C64B3C1BE23B__INCLUDED_)
