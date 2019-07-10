#include <cassert>
#include <string>
#include <vector>
#include <cstring>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <iomanip>
#include <sstream>
#include <regex>
#include "acc_user.h"
#include "sv_vpi_user.h"
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string.hpp>

using namespace std;

static bool FinishSet = false;

static ofstream rtlFile;

static vector<regex> ignoredInstances, ignoredModules;
static vector<regex> definedModules;
static bool ignoreModWithSpChars;

static const
vector<PLI_INT32> modTypes({ vpiModule, vpiModuleArray, vpiPrimitive });
static const
vector<PLI_INT32> netTypes({ vpiPort, vpiNet, vpiReg, vpiBit });
static const
vector<PLI_INT32> arrTypes({ vpiNetArray, vpiArrayVar });

static inline bool
nameHasSpecialChars(const string &name)
{
  return ( 0 == count_if ( name.begin(),
                           name.end(),
                           [](char c){ return !(isalnum(c) || ('_' == c)); }
                           ) ) ? false : true;
}

static void
addNet(vpiHandle net, PLI_INT32 nettype, bool isRoot = false)
{
  auto name = vpi_get_str(vpiName, net);
  auto size = vpi_get(vpiSize, net);
  auto type = vpi_get(vpiDirection, net);

  auto fType = ((isRoot) && (vpiInput == type)) ? vpiInput :
    ((isRoot) && (vpiOutput == type)) ? vpiOutput :
    (vpiInput == type) ? vpiNet :
    (vpiOutput == type) ? vpiNet :
    (vpiNet == nettype) ? vpiNet :
    vpiBitVar;

  rtlFile << ((vpiInput == fType) ? "  input " :
              (vpiOutput == fType) ? "  output bit " : "  bit ");
  if (size>1)
    rtlFile << "[" << size-1 << " : 0] ";
  if (vpiNet == fType)
    rtlFile << "netbit_";
  rtlFile << name;

  if (vpiNet == fType) {
    rtlFile << ";" << endl << "  wire ";
    if (size>1)
      rtlFile << "[" << size-1 << " : 0] ";
    rtlFile << name << " = netbit_" << name;
  }

}

static void
addArrNet(vpiHandle net, PLI_INT32 nettype, bool isRoot = false)
{
  PLI_INT32 idx_arr[] = {0, 0, 0, 0, 0, 0, 0, 0};
  auto name = vpi_get_str(vpiName, net);
  auto size = vpi_get(vpiSize, net);
  auto type = vpi_get(vpiDirection, net);

  rtlFile << (
              ((isRoot) && (vpiInput == type)) ? "  input " :
              ((isRoot) && (vpiOutput == type)) ? "  output bit " :
              "  bit ");

  /* # indexes */
  vpiHandle range = vpi_iterate(vpiRange, net);
  auto ui = 0;
  while (vpi_scan(range)) ui++;
  if (ui>1) {
    vpiHandle obj = vpi_handle_by_multi_index(net, ui, idx_arr);
    rtlFile << " [" << (vpi_get(vpiSize, obj)-1) << " : 0]";
    vpi_free_object(obj);
  } else if (ui) {
    vpiHandle obj = vpi_handle_by_index(net, ui);
    rtlFile << "[" << (vpi_get(vpiSize, obj)-1) << " : 0]";
    vpi_free_object(obj);
  }
  assert(ui < 6);
  rtlFile << name;
  range = vpi_iterate(vpiRange, net);
  for (vpiHandle ri = vpi_scan(range); ri != NULL;
       ri = vpi_scan(range)) {
    rtlFile << " [" << (vpi_get(vpiSize, ri)-1) << " : 0]";
  }
  rtlFile << " /* addArrNet */";
}

static set<string> modUnique, builtinMods;

static void
elabModule(vpiHandle root, bool isRoot = false)
{
  assert(NULL != root);

  /* Check if we have already elaborated the module name */
  string modName = vpi_get_str(vpiDefName, root);
  if ( (modUnique.end() != modUnique.find(modName)) ||
       (builtinMods.end() != builtinMods.find(modName)) )
    return;
  modUnique.insert(modName);

  /* create inst modules */
  for (auto modtype : modTypes) {
    vpiHandle iterator = vpi_iterate(modtype, root);
    if (NULL == iterator) {
      continue;
    }
    for (vpiHandle thing = vpi_scan(iterator);
         thing != NULL; thing = vpi_scan(iterator)) {
      bool isIgnoredInstance = false;
      string iName = vpi_get_str(vpiName, thing);
      string mName = vpi_get_str(vpiDefName, thing);

      /* ignored modules */
      if (ignoreModWithSpChars && nameHasSpecialChars(mName))
        continue;
      if (modUnique.end() != modUnique.find(mName))
        continue;
      if (builtinMods.end() != builtinMods.find(mName))
        continue;
      for (auto &im : ignoredModules) {
        if (regex_match(mName, im)) {
          isIgnoredInstance = true;
          break;
        }
      }
      if (isIgnoredInstance) continue;
      for (auto &ii : ignoredInstances) {
        if (regex_match(iName, ii)) {
          isIgnoredInstance = true;
          break;
        }
      }
      if (isIgnoredInstance) continue;
      for (auto &im : definedModules) {
        if (regex_match(mName, im)) {
          modUnique.insert(mName);
          isIgnoredInstance = true;
          break;
        }
      }
      if (isIgnoredInstance) continue;

      /* */
      elabModule(thing);
    }
  }

  /* Start module def, dump ports */
  set<string> sigUnique;
  rtlFile << "module " << modName;
  if (!isRoot) {
    rtlFile << ";" << endl;
  } else {
    rtlFile << " (" << endl;
    vpiHandle iterator = vpi_iterate(vpiPort, root);
    if (NULL != iterator) {
      bool loop1 = true;
      for (vpiHandle thing = vpi_scan(iterator);
           thing != NULL; thing = vpi_scan(iterator)) {
        if (!loop1)
          rtlFile << "," << endl;
        loop1 = false;
        auto sn = vpi_get_str(vpiName, thing);
        sigUnique.insert(sn);
        if (nameHasSpecialChars(sn)) continue;
        addNet(thing, vpiPort, isRoot);
      }
    }
    rtlFile << "  );" << endl;
  }

  /* Dump instances */
  for (auto modtype : modTypes) {
    vpiHandle iterator = vpi_iterate(modtype, root);
    if (NULL == iterator) {
      continue;
    }
    for (vpiHandle thing = vpi_scan(iterator);
         thing != NULL; thing = vpi_scan(iterator)) {
      bool isIgnoredInstance = false;
      string iName = vpi_get_str(vpiName, thing);
      string mName = vpi_get_str(vpiDefName, thing);

      /* ignored modules */
      if (ignoreModWithSpChars && nameHasSpecialChars(iName))
        continue;
      if (modUnique.end() == modUnique.find(mName))
        continue;
      if (builtinMods.end() != builtinMods.find(mName))
        continue;
      for (auto &im : ignoredModules) {
        if (regex_match(mName, im)) {
          isIgnoredInstance = true;
          break;
        }
      }
      if (isIgnoredInstance) continue;
      for (auto &ii : ignoredInstances) {
        if (regex_match(iName, ii)) {
          isIgnoredInstance = true;
          break;
        }
      }
      if (isIgnoredInstance) continue;

      rtlFile << "  " << mName << " " << iName << "();" << endl;
    }
  }

  /* Dump nets */
  for (auto nettype : netTypes) {
    /* already dumped */
    if (isRoot && (vpiPort == nettype))
      continue;
 
    vpiHandle iterator = vpi_iterate(nettype, root);
    if (NULL == iterator) {
      continue;
    }
    for (vpiHandle thing = vpi_scan(iterator);
         thing != NULL; thing = vpi_scan(iterator)) {
      auto sn = vpi_get_str(vpiName, thing);
      if (sigUnique.end() != sigUnique.find(sn))
        continue;
      sigUnique.insert(sn);

      if (nameHasSpecialChars(sn)) continue;
      addNet(thing, nettype, isRoot);
      rtlFile << ";" << endl;
    }
  }

  /* Dump MDAs */
  for (auto nettype : arrTypes) {
    /* already dumped */
    if (isRoot && (vpiPort == nettype))
      continue;
 
    vpiHandle iterator = vpi_iterate(nettype, root);
    if (NULL == iterator) {
      continue;
    }
    for (vpiHandle thing = vpi_scan(iterator);
         thing != NULL; thing = vpi_scan(iterator)) {
      auto sn = vpi_get_str(vpiName, thing);
      if (sigUnique.end() != sigUnique.find(sn))
        continue;
      sigUnique.insert(sn);

      if (nameHasSpecialChars(sn)) continue;
      addArrNet(thing, nettype, isRoot);
      rtlFile << ";" << endl;
    }
  }

  /* */
  rtlFile << "endmodule // " << modName << endl;

  return;
}

extern "C" PLI_INT32
genRtlElabRoot(p_cb_data cb_data_p)
{
  /* */
  char *topHier = nullptr, *fileName = nullptr;
  auto argv = acc_fetch_argv();
  auto argc = acc_fetch_argc();
  ignoreModWithSpChars = false;

  /* get prog opts */
  for (auto argi = 1; argi<argc; argi++) {
    if (boost::starts_with(argv[argi], "+genRtl+hier=")) {
      topHier = ((char *)(argv[argi]))+13;
    } else if (boost::starts_with(argv[argi], "+genRtl+file=")) {
      fileName = ((char *)(argv[argi]))+13;
    } else if (boost::starts_with(argv[argi], "+genRtl+ignoreInst=")) {
      ignoredInstances.push_back(regex(((char *)(argv[argi]))+19));
    } else if (boost::starts_with(argv[argi], "+genRtl+ignoreMod=")) {
      ignoredModules.push_back(regex(((char *)(argv[argi]))+18));
    } else if (boost::starts_with(argv[argi], "+genRtl+definedMod=")) {
      definedModules.push_back(regex(((char *)(argv[argi]))+19));
    } else if (boost::starts_with(argv[argi], "+genRtl+ignoreModWithSpChars")) {
      ignoreModWithSpChars = true;
    }
  }
  if ((nullptr == topHier) || (nullptr == fileName)) {
    /* Simulation is not for dump of hier */
    return 0;
  }

  vpiHandle root;
  root = vpi_handle_by_name(topHier, NULL);
  s_vpi_error_info info;
  int level;

  memset(&info, 0, sizeof(info));
  level = vpi_chk_error(&info);
  if ((0 != info.code) || (0 != level)) {
    cerr << "VPI Error:" << info.message << endl;
    cerr << "\tPROD:" << info.product << endl;
    cerr << "\tCODE:" << info.code << " FILE:" << info.file << endl;

    vpi_control(vpiFinish, __LINE__);
    return 0;
  }

  /* init */
  builtinMods.insert("not"); builtinMods.insert("buf");
  builtinMods.insert("and"); builtinMods.insert("nand");
  builtinMods.insert("nor"); builtinMods.insert("or");
  builtinMods.insert("xor"); builtinMods.insert("xnor");
  builtinMods.insert("bufif0"); builtinMods.insert("bufif1");
  builtinMods.insert("notif0"); builtinMods.insert("notif1");
  builtinMods.insert("nmos"); builtinMods.insert("pmos");
  builtinMods.insert("rnmos"); builtinMods.insert("rpmos");
  builtinMods.insert("cmos"); builtinMods.insert("rcmos");
  builtinMods.insert("rtranif0"); builtinMods.insert("rtranif1");
  builtinMods.insert("tranif0"); builtinMods.insert("tranif1");
  builtinMods.insert("tran"); builtinMods.insert("rtran");
  builtinMods.insert("pullup"); builtinMods.insert("pulldown");

  /* parse through design */
  rtlFile.open(fileName, ofstream::binary);
  elabModule(root, true);
  vpi_free_object(root);
  rtlFile.close();

  vpi_control(vpiFinish, 0);
  return 0;
}

extern "C" PLI_INT32
genRtlFinish(p_cb_data cb_data_p)
{
}

extern "C" PLI_INT32
genRtlElabRoot1(p_cb_data cb_data_p)
{
  PLI_INT32 idx_arr[] = {0, 0, 0, 0, 0, 0, 0, 0};
  vpiHandle topMod = vpi_iterate(vpiModule, NULL);
  vpiHandle root = vpi_scan(topMod);

  cout << "top:" << vpi_get_str(vpiName, root) << endl;

  /* vpiNetArray, vpiReg, vpiRegArray
     vpiBitVar, vpiArrayVar, vpiPackedArrayVar, vpiPackedArrayTypespec, vpiArrayType, vpiBitTypespec, vpiLogicTypespec, vpiArrayTypespec, vpiPackedArrayTypespec, 
   */
  vpiHandle top = vpi_iterate(vpiNetArray, root);
  for (vpiHandle thing = vpi_scan(top);
       thing != NULL; thing = vpi_scan(top)) {
    auto name = vpi_get_str(vpiName, thing);
    auto type = vpi_get(vpiType, thing);
    vpiHandle rh = vpi_iterate(vpiRange, thing);
    auto ui = 0;
    cout << name;
    for (vpiHandle rh_i = vpi_scan(rh); rh_i != NULL;
         rh_i = vpi_scan(rh)) {
      ui ++;
      cout << " " << vpi_get(vpiSize, rh_i);
    }
    if (ui>1) {
      vpiHandle obj = vpi_handle_by_multi_index(thing, ui, idx_arr);
      cout << "[" << vpi_get(vpiSize, obj) << "]";
      vpi_free_object(obj);
    } else if (ui) {
      vpiHandle obj = vpi_handle_by_index(thing, 1);
      cout << "[" << vpi_get(vpiSize, obj) << "]";
      vpi_free_object(obj);
    }
    cout << endl;
  }

  vpi_free_object(root);
  vpi_free_object(topMod);
  vpi_control(vpiFinish, 0);
  return 0;
}

extern "C" PLI_INT32
register_elaboration_callback(void)
{
  s_cb_data callback;

  assert (!FinishSet);

  callback.reason = cbStartOfSimulation;
  callback.cb_rtn = genRtlElabRoot;
  callback.user_data = 0;
  vpi_register_cb(&callback);

  callback.reason = cbEndOfSimulation;
  callback.cb_rtn = genRtlFinish;
  callback.user_data = 0;
  vpi_register_cb(&callback);

  return 0;
}
