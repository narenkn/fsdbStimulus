#include "fsdb2rtl.h"

svBitT::svBitT(uint32_t S, char *n)
  : svType(S, n)
{
  Value = NULL;
  assert(S);
}

void
svBitT::size(uint32_t S)
{
  uWordSize = S;
  uArraySize = VPI_CANONICAL_SIZE(S);

  Value = new t_vpi_vecval[uArraySize];
}

void
svBitT::update(byte_T *vcptr)
{
  /* Update object to the value ... */
  uint32_t ui3, ui4, ui5, ui7;
  PLI_UINT32 aval, bval;

  for (ui4=0, ui5=0;
       ui4<uWordSize;
       ui4+=BitsInAval, ui5++) {
    bval = 0; aval = 0;

    ui7 = ((ui4+BitsInAval) > uWordSize) ? uWordSize :
      ui4+BitsInAval;
    for (ui3=ui4; ui3<ui7; ui3++) {
      switch(vcptr[uWordSize-ui3-1]) {
      case FSDB_BT_VCD_X:
        aval |= 1<<(ui3-ui4);
      case FSDB_BT_VCD_Z:
        bval |= 1<<(ui3-ui4);
        break;
      case FSDB_BT_VCD_1:
        aval |= 1<<(ui3-ui4);
        break;
      }
    }

    Value[ui5].aval = aval;
    Value[ui5].bval = bval;
  }

  /* */
  s_vpi_value value;
  value.format = vpiVectorVal;
  value.value.vector = Value;
  assert(vpih);
  vpi_put_value(vpih, &value, NULL, vpiNoDelay);
}

svBitT::~svBitT()
{
  if (Value)
    delete [] Value;
}

svLogicT::svLogicT(uint32_t S, char *n)
  : svType(S, n)
{
  Value = NULL;
  assert(S);
}

void
svLogicT::size(uint32_t S)
{
  uWordSize = S;
  uArraySize = VPI_CANONICAL_SIZE(S);

  Value = new t_vpi_vecval[uArraySize];
}

void
svLogicT::update(byte_T *vcptr)
{
  /* Update object to the value ... */
  uint32_t ui3, ui4, ui5, ui7;
  PLI_UINT32 aval, bval;

  //cout << "fsdb2rtl: updating '" << rtlName << "'" << endl;

  for (ui4=0, ui5=0;
       ui4<uWordSize;
       ui4+=BitsInAval, ui5++) {
    bval = 0; aval = 0;

    ui7 = ((ui4+BitsInAval) > uWordSize) ? uWordSize :
      ui4+BitsInAval;
    for (ui3=ui4; ui3<ui7; ui3++) {
      switch(vcptr[uWordSize-ui3-1]) {
      case FSDB_BT_VCD_X:
        aval |= 1<<(ui3-ui4);
      case FSDB_BT_VCD_Z:
        bval |= 1<<(ui3-ui4);
        break;
      case FSDB_BT_VCD_1:
        aval |= 1<<(ui3-ui4);
        break;
      }
    }

    Value[ui5].aval = aval;
    Value[ui5].bval = bval;
  }

  /* */
  s_vpi_value value;
  value.format = vpiVectorVal;
  value.value.vector = Value;
  assert(vpih);
  vpi_put_value(vpih, &value, NULL, vpiNoDelay);
}

svLogicT::~svLogicT()
{
  if (Value)
    delete [] Value;
}

static bool FinishSet = false;
static bool DivSimTime = false;
static uint64_t SimTimeScale = 0;
static uint64_t SimTime, OldSimTime = -1;

static vector<string> rtl2fsdbmap;
static unordered_map<string, uint64_t> fsdb2arr;
static unordered_map<fsdbVarIdcode, uint64_t> idcode2idx;
static vector<svType *> rtlSigs;
static ffrTimeBasedVCTrvsHdl tbTrvsHndl = NULL;

static const
vector<PLI_INT32> modTypes({ vpiModule, vpiModuleArray, vpiPrimitive });
static const
vector<PLI_INT32> netTypes({ vpiPort, vpiNet, vpiReg, vpiBit });

static string fsdbFile;
static ffrObject* fsdbObj = NULL;

static void Callback();
static void ScheduleSyncCallback();

EXTERN PLI_INT32
Fsdb2RtlSyncCallback(p_cb_data cb_data)
{
  PLI_INT32 reason = cb_data->reason;

  if (SimTimeScale == 0) {
    int simTimePrecision = vpi_get(vpiTimePrecision, NULL);
    int simTimeScaleExp = VPI_CB_TIMESCALE - simTimePrecision;

    cout << "Fsdb2Rtl:Info vpiTimePrecision=" << simTimePrecision << endl;

    if (simTimeScaleExp >= 0) {
      DivSimTime = true;
      SimTimeScale = pow(10.0, simTimeScaleExp);
    } else {
      DivSimTime = false;
      SimTimeScale = pow(10.0, -1 * simTimeScaleExp);
    }
  }

  /* Now time */
  SimTime = (uint64_t(cb_data->time->high)<<32) | cb_data->time->low;
  if (DivSimTime) {
    SimTime = SimTime / SimTimeScale;
  } else {
    SimTime = SimTime * SimTimeScale;
  }

  switch(reason) {
    case cbReadWriteSynch:
      //cout << "Called in cbReadWriteSynch @" << SimTime << endl;
      Callback();
      break;

    case cbAfterDelay:
      //cout << "Called in cbAfterDelay @" << SimTime << endl;
      ScheduleSyncCallback();
      break;

    default:
      cerr << "Fsdb2Rtl: Error: " << " called for unexpected reason" << reason << endl;
      vpi_control(vpiFinish, __LINE__);
      FinishSet = true;
      break;
  }

  return 0;
}

static void
ScheduleSyncCallback()
{
  vpiHandle  cbHandle;
  s_vpi_time vpiTimeObj;
  s_cb_data  cbData;

  //cout << "ScheduleSyncCallback called" << endl;
  vpiTimeObj.type = vpiSimTime;
  vpiTimeObj.low  = 0;
  vpiTimeObj.high = 0;

  cbData.reason    = cbReadWriteSynch;
  cbData.cb_rtn    = Fsdb2RtlSyncCallback;
  cbData.time      = &vpiTimeObj;
  cbData.value     = NULL;
  cbData.obj       = NULL;
  cbData.user_data = NULL;

  cbHandle = vpi_register_cb(&cbData);
  vpi_free_object(cbHandle);
}

static void
ScheduleAfterDelayCallback(uint64_t delay)
{
  vpiHandle  cbHandle;
  s_vpi_time vpiTimeObj;
  s_cb_data  cbData;

  assert(! FinishSet);

  //cout << "ScheduleAfterDelayCallback after " << delay << endl;
  vpiTimeObj.type = vpiSimTime;
  vpiTimeObj.low  = delay & 0xffffffff;
  vpiTimeObj.high = delay >> 32;

  cbData.reason    = cbAfterDelay;
  cbData.cb_rtn    = Fsdb2RtlSyncCallback;
  cbData.time      = &vpiTimeObj;
  cbData.value     = NULL;
  cbData.obj       = NULL;
  cbData.user_data = NULL;

  cbHandle = vpi_register_cb(&cbData);
  vpi_free_object(cbHandle);
}

/* Return the delay time of next call back */
static void
Callback()
{
  static fsdbXTag cur_time, nxt_time;
  byte_T *vcptr;
  fsdbVarIdcode idcode;
  svType *rsig;

  if (FinishSet) return;

  /* Find the next time instant in FSDB and cause a callback at that time */
 CallbackStartOver:
  /* make assignment */
  if (FSDB_RC_SUCCESS != tbTrvsHndl->ffrGetVC(&vcptr))
    goto CallbackEndOver;
  tbTrvsHndl->ffrGetVarIdcode(&idcode);
  //cout << "Fsdb2Rtl @" << SimTime << " Idcode:" << idcode << endl;
  assert(idcode2idx.find(idcode) != idcode2idx.end());
  rsig = rtlSigs[idcode2idx[idcode]];
  rsig->update(vcptr);
  if (FSDB_RC_SUCCESS == tbTrvsHndl->ffrGotoNextVC()) {
    tbTrvsHndl->ffrGetXTag(&nxt_time);
    if ((cur_time.hltag.L != nxt_time.hltag.L) ||
        (cur_time.hltag.H != nxt_time.hltag.H)) {
      uint64_t delta = (((uint64_t)(nxt_time.hltag.H - cur_time.hltag.H))<<32) | (nxt_time.hltag.L - cur_time.hltag.L);
      ScheduleAfterDelayCallback(delta);
    } else
      goto CallbackStartOver;
  } else
      goto CallbackEndOver;

  /* verbose */
//    cout << "Fsdb2Rtl: SimTime:" << SimTime << " cur_time:" <<
//      ((((uint64_t)cur_time.hltag.H)<<32)|cur_time.hltag.L) <<
//      " nxt_time:" <<
//      ((((uint64_t)nxt_time.hltag.H)<<32)|nxt_time.hltag.L) <<
//      endl;
  cur_time = nxt_time;
  return;

 CallbackEndOver:
    cout << "Fsdb2Rtl: End of FSDB, Finishing simulation" << endl;
    vpi_control(vpiFinish, 0);
    FinishSet = true;
}

static bool_T
MyTreeCBFunc (fsdbTreeCBType cb_type, void *client_data, void *cb_data)
{
  fsdbTreeCBDataScope *scope;
  fsdbTreeCBDataVar *var;
  uint64_t ui1, ui2, ui3, ui4;
  static string current_hier;

  switch (cb_type) {
  case FSDB_TREE_CBT_BEGIN_TREE:
    current_hier = "";
    break;
  case FSDB_TREE_CBT_SCOPE:
    scope = (fsdbTreeCBDataScope*) cb_data;
    if (!current_hier.empty())
      current_hier += ".";
    current_hier += (char *) scope->name;
    break;
  case FSDB_TREE_CBT_UPSCOPE:
    ui1 = current_hier.rfind(".");
    if (ui1 == string::npos)
      current_hier = "";
    else
      current_hier.resize(ui1);
    break;
  case FSDB_TREE_CBT_VAR:
    {
      var = (fsdbTreeCBDataVar*)cb_data;
      string sig = current_hier;
      if (!current_hier.empty())
        sig += '.';
      /* Remove trailing [] */
      string s1 = (char *) (var->name);
      ui1 = s1.rfind("[");
      do {
        if (ui1 == string::npos)
          break;
        /* Remove [] */
        if (sscanf((s1.c_str() + ui1 + 1), "%d:%d", &ui2, &ui3)
            == 1) {
          ui2--; ui3--;
        }
        s1.resize(ui1);
      } while (0);
      sig += s1;
      if (fsdb2arr.find(sig) != fsdb2arr.end()) {
        auto sigIdx = fsdb2arr[sig];
        auto rsig = rtlSigs[sigIdx];
        rsig->idcode = var->u.idcode;
      }
    }
    break;
  default:
    break;
  }

  return true;
}

static void
checkVpiError(void)
{
  s_vpi_error_info info;
  int level;

  memset(&info, 0, sizeof(info));
  level = vpi_chk_error(&info);
  if (info.code == 0 && level == 0)
    return;

  cerr << "VPI Error:" << info.message << endl;
  cerr << "\tPROD:" << info.product << endl;
  cerr << "\tCODE:" << info.code << " FILE:" << info.file << endl;

  vpi_control(vpiFinish, __LINE__);
  FinishSet = true;
}

static void
addNet(vpiHandle net, PLI_INT32 nettype)
{
  char *name = vpi_get_str(vpiFullName, net);

  for ( auto str = rtl2fsdbmap.begin();
        str != rtl2fsdbmap.end(); str+=2 ) {
    if ((str+1) == rtl2fsdbmap.end())
      break;
    //cout << "Compare '" << *str << "' vs '" << name << "' len:" << (*str).size() << endl;
    if (0 == (*(str+1)).compare(0, string::npos, name, (*(str+1)).size())) {
      fsdb2arr.insert(make_pair<string, uint64_t>((*str) + ((char *)(name+(*(str+1)).size())), rtlSigs.size()));
      auto size = vpi_get(vpiSize, net);
      svType *rsig = (vpiBit == nettype) ? (svType *)new svBitT(size, name) : (svType *)new svLogicT(size, name);
      rtlSigs.push_back(rsig);
      //cout << "Inserting : " << (*str) + ((char *)(name+(*str).size())) << endl;

      /* */
      rsig->Attach();
    }
  }
}

static PLI_INT32
elabModule(vpiHandle root)
{
  assert(NULL != root);

  /* iterate modules */
  for (auto modtype : modTypes) {
    vpiHandle iterator = vpi_iterate(modtype, root);
    if (NULL == iterator) {
      continue;
    }
    for (vpiHandle thing = vpi_scan(iterator);
         thing != NULL; thing = vpi_scan(iterator)) {
      elabModule(thing);
    }
  }

  /* Add nets */
  for (auto nettype : netTypes) {
    vpiHandle iterator = vpi_iterate(nettype, root);
    if (NULL == iterator) {
      continue;
    }
    for (vpiHandle thing = vpi_scan(iterator);
         thing != NULL; thing = vpi_scan(iterator)) {
      addNet(thing, nettype);
    }
  }

  return 0;
}

EXTERN PLI_INT32
Fsdb2RtlElabRoot(p_cb_data cb_data_p)
{
  /* */
  vpiHandle iterator;

  iterator = vpi_iterate(vpiModule, NULL);
  checkVpiError();

  /* */
  auto argv = acc_fetch_argv();
  auto argc = acc_fetch_argc();
  for (auto argi=1; argi<argc; argi++) {
    if (boost::starts_with(argv[argi], "+fsdb2rtl+fsdb=")) {
      fsdbFile = ((char *)(argv[argi]))+15;
    } else if (boost::starts_with(argv[argi], "+fsdb2rtl")) {
      auto argv_len = strlen(argv[argi]);
      std::string arg(((argv[argi])+10));
      boost::split(rtl2fsdbmap, arg, boost::is_any_of("+="), boost::token_compress_on);
      for ( auto str = rtl2fsdbmap.begin();
            str != rtl2fsdbmap.end(); str+=2 ) {
        if ((str+1) == rtl2fsdbmap.end())
          break;
        //cout << "Found RTL map: +" << *str << "=" << *(str+1) << endl;
      }
    }
  }

  /* Essentials */
  if (0 == fsdbFile.size()) {
    cerr << "Error:Fsdb2Rtl: Didn't find +fsdb2rtl+fsdb= option on commandline" << endl;
    vpi_control(vpiFinish, __LINE__);
    FinishSet = true;
    return 0;
  }
  if (0 == rtl2fsdbmap.size()) {
    cerr << "Error:Fsdb2Rtl: Didn't find +fsdb2rtl+<rtlhier>=<fsdbhier> on commandline" << endl;
    vpi_control(vpiFinish, __LINE__);
    FinishSet = true;
    return 0;
  }
  if (0 == ffrObject::ffrIsFSDB((str_T)fsdbFile.c_str())) {
    cerr << "Error:Fsdb2Rtl: Not an FSDB file : " << fsdbFile << endl;
    vpi_control(vpiFinish, __LINE__);
    FinishSet = true;
    return 0;
  }

  /* parse through design */
  for (vpiHandle root = vpi_scan(iterator);
       root != NULL; root = vpi_scan(iterator)) {
    elabModule(root);
  }
  if (0 == rtlSigs.size()) {
    cerr << "Fsdb2Rtl: Error: No matching RTL signals found" << endl;
    vpi_control(vpiFinish, __LINE__);
    FinishSet = true;
    return 0;
  }

  /* Open FSDB and parse */
  fsdbObj = ffrObject::ffrOpen2((str_T)fsdbFile.c_str(), MyTreeCBFunc, NULL);
  if (NULL == fsdbObj) {
    cerr << "Error: Fsdb2Rtl: opening FSDB file : " << fsdbFile << endl;
    vpi_control(vpiFinish, __LINE__);
    return 0;
  }

  /* Read all signals from FSDB */
  fsdbObj->ffrReadScopeVarTree();

  /* */
  vector<uint64_t> sigs2remove;
  auto validSigs = 0;
  for (auto &f2a: fsdb2arr) {
    auto rsig = rtlSigs[f2a.second];
    if (-1 == rsig->idcode) {
      cerr << "Fsdb2Rtl: Warning: Can't find signal '" << f2a.first << "' in FSDB" << endl;
      f2a.second = -1;
    } else {
      fsdbObj->ffrAddToSignalList(rsig->idcode);
      validSigs++;
    }
  }
  fsdbObj->ffrLoadSignals();
  for (auto &f2a: fsdb2arr) {
    if (-1 == f2a.second)
      continue;
    auto rsig = rtlSigs[f2a.second];
    //    cout << "Fsdb2Rtl: idcode is " << rsig->idcode << endl;
    rsig->var_hdl = fsdbObj->ffrCreateVCTraverseHandle(rsig->idcode);
    if (FSDB_BYTES_PER_BIT_1B !=
        rsig->var_hdl->ffrGetBytesPerBit()) {
      cerr << "Fsdb2Rtl: Error: ffrGetBytesPerBit() : contact narenkn@gmail.com for a fix" << endl;
      vpi_control(vpiFinish, __LINE__);
      FinishSet = true;
      return 0;
    }
    auto fbits = rsig->var_hdl->ffrGetBitSize();
    if (fbits != rsig->uWordSize) {
      cerr << "Fsdb2Rtl: Warning:" << f2a.first <<
        " width mismatch RTL:" <<  rsig->uWordSize <<
        " FSDB:" << fbits << endl;
      /* ignore this signal also, as it crashes */
      f2a.second = -1;
      rsig->idcode = -1;
      validSigs--;
    }
    rsig->size(rsig->uWordSize >= fbits ? fbits : rsig->uWordSize);

    //cout << "Fsdb2Rtl: Connected " << f2a.first << ":" << rsig->uWordSize << endl;
  }

  auto idcodes = new fsdbVarIdcode[validSigs];
  auto ididx = 0;
  for (auto &rsig: rtlSigs) {
    auto idcode = rsig->idcode;
    if (-1 == rsig->idcode)
      continue;
    idcode2idx[idcode] = ididx;
    idcodes[ididx++] = idcode;
  }
  assert(ididx == validSigs);
  assert(ididx <= rtlSigs.size());
  tbTrvsHndl = fsdbObj->ffrCreateTimeBasedVCTrvsHdl(ididx, idcodes);
  delete [] idcodes;

  /* */
  Callback();

  return 0;
}

EXTERN PLI_INT32
Fsdb2RtlFinish(p_cb_data cb_data_p)
{
  for (auto &rsig: rtlSigs) {
    delete rsig;
  }

  if (NULL != tbTrvsHndl)
    tbTrvsHndl->ffrFree();
  if (NULL != fsdbObj) {
    fsdbObj->ffrUnloadSignals();
    fsdbObj->ffrClose();
  }
}

EXTERN PLI_INT32
Fsdb2RtlElabCb(void)
{
  s_cb_data callback;

  assert (!FinishSet);

  callback.reason = cbStartOfSimulation;
  callback.cb_rtn = Fsdb2RtlElabRoot;
  callback.user_data = 0;
  vpi_register_cb(&callback);

  callback.reason = cbEndOfSimulation;
  callback.cb_rtn = Fsdb2RtlFinish;
  callback.user_data = 0;
  vpi_register_cb(&callback);

  return 0;
}
