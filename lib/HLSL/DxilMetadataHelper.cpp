///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// DxilMetadataHelper.cpp                                                    //
// Copyright (C) Microsoft Corporation. All rights reserved.                 //
// This file is distributed under the University of Illinois Open Source     //
// License. See LICENSE.TXT for details.                                     //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

#include "dxc/Support/Global.h"
#include "dxc/HLSL/DxilMetadataHelper.h"
#include "dxc/HLSL/DxilShaderModel.h"
#include "dxc/HLSL/DxilCBuffer.h"
#include "dxc/HLSL/DxilResource.h"
#include "dxc/HLSL/DxilSampler.h"
#include "dxc/HLSL/DxilSignatureElement.h"
#include "dxc/HLSL/DxilSignature.h"
#include "dxc/HLSL/DxilTypeSystem.h"
#include "dxc/HLSL/DxilRootSignature.h"
#include "dxc/HLSL/ComputeViewIdState.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/raw_ostream.h"
#include <array>
#include <algorithm>

#include "dxc/Support/WinIncludes.h"

using namespace llvm;
using std::string;
using std::vector;
using std::unique_ptr;


namespace hlsl {

const char DxilMDHelper::kDxilVersionMDName[]                         = "dx.version";
const char DxilMDHelper::kDxilShaderModelMDName[]                     = "dx.shaderModel";
const char DxilMDHelper::kDxilEntryPointsMDName[]                     = "dx.entryPoints";
const char DxilMDHelper::kDxilResourcesMDName[]                       = "dx.resources";
const char DxilMDHelper::kDxilTypeSystemMDName[]                      = "dx.typeAnnotations";
const char DxilMDHelper::kDxilTypeSystemHelperVariablePrefix[]        = "dx.typevar.";
const char DxilMDHelper::kDxilControlFlowHintMDName[]                 = "dx.controlflow.hints";
const char DxilMDHelper::kDxilPreciseAttributeMDName[]                = "dx.precise";
const char DxilMDHelper::kHLDxilResourceAttributeMDName[]             = "dx.hl.resource.attribute";
const char DxilMDHelper::kDxilValidatorVersionMDName[]                = "dx.valver";

// This named metadata is not valid in final module (should be moved to DxilContainer)
const char DxilMDHelper::kDxilRootSignatureMDName[]                   = "dx.rootSignature";
const char DxilMDHelper::kDxilViewIdStateMDName[]                     = "dx.viewIdState";

static std::array<const char *, 7> DxilMDNames = {
  DxilMDHelper::kDxilVersionMDName,
  DxilMDHelper::kDxilShaderModelMDName,
  DxilMDHelper::kDxilEntryPointsMDName,
  DxilMDHelper::kDxilResourcesMDName,
  DxilMDHelper::kDxilTypeSystemMDName,
  DxilMDHelper::kDxilValidatorVersionMDName,
  DxilMDHelper::kDxilViewIdStateMDName
};

DxilMDHelper::DxilMDHelper(Module *pModule, std::unique_ptr<ExtraPropertyHelper> EPH)
: m_pModule(pModule)
, m_Ctx(pModule->getContext())
, m_pSM(nullptr)
, m_ExtraPropertyHelper(std::move(EPH)) {
}

DxilMDHelper::~DxilMDHelper() {
}

void DxilMDHelper::SetShaderModel(const ShaderModel *pSM) {
  m_pSM = pSM;
}

const ShaderModel *DxilMDHelper::GetShaderModel() const {
  return m_pSM;
}

//
// DXIL version.
//
void DxilMDHelper::EmitDxilVersion(unsigned Major, unsigned Minor) {
  NamedMDNode *pDxilVersionMD = m_pModule->getNamedMetadata(kDxilVersionMDName);
  IFTBOOL(pDxilVersionMD == nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  pDxilVersionMD = m_pModule->getOrInsertNamedMetadata(kDxilVersionMDName);

  Metadata *MDVals[kDxilVersionNumFields];
  MDVals[kDxilVersionMajorIdx] = Uint32ToConstMD(Major);
  MDVals[kDxilVersionMinorIdx] = Uint32ToConstMD(Minor);

  pDxilVersionMD->addOperand(MDNode::get(m_Ctx, MDVals));
}

void DxilMDHelper::LoadDxilVersion(unsigned &Major, unsigned &Minor) {
  NamedMDNode *pDxilVersionMD = m_pModule->getNamedMetadata(kDxilVersionMDName);
  IFTBOOL(pDxilVersionMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  IFTBOOL(pDxilVersionMD->getNumOperands() == 1, DXC_E_INCORRECT_DXIL_METADATA);

  MDNode *pVersionMD = pDxilVersionMD->getOperand(0);
  IFTBOOL(pVersionMD->getNumOperands() == kDxilVersionNumFields, DXC_E_INCORRECT_DXIL_METADATA);

  Major = ConstMDToUint32(pVersionMD->getOperand(kDxilVersionMajorIdx));
  Minor = ConstMDToUint32(pVersionMD->getOperand(kDxilVersionMinorIdx));
}

//
// Validator version.
//
void DxilMDHelper::EmitValidatorVersion(unsigned Major, unsigned Minor) {
  NamedMDNode *pDxilValidatorVersionMD = m_pModule->getNamedMetadata(kDxilValidatorVersionMDName);

  // Allow re-writing the validator version, since this can be changed at later points.
  if (pDxilValidatorVersionMD)
    m_pModule->eraseNamedMetadata(pDxilValidatorVersionMD);

  pDxilValidatorVersionMD = m_pModule->getOrInsertNamedMetadata(kDxilValidatorVersionMDName);

  Metadata *MDVals[kDxilVersionNumFields];
  MDVals[kDxilVersionMajorIdx] = Uint32ToConstMD(Major);
  MDVals[kDxilVersionMinorIdx] = Uint32ToConstMD(Minor);

  pDxilValidatorVersionMD->addOperand(MDNode::get(m_Ctx, MDVals));
}

void DxilMDHelper::LoadValidatorVersion(unsigned &Major, unsigned &Minor) {
  NamedMDNode *pDxilValidatorVersionMD = m_pModule->getNamedMetadata(kDxilValidatorVersionMDName);

  if (pDxilValidatorVersionMD == nullptr) {
    // If no validator version metadata, assume 1.0
    Major = 1;
    Minor = 0;
    return;
  }

  IFTBOOL(pDxilValidatorVersionMD->getNumOperands() == 1, DXC_E_INCORRECT_DXIL_METADATA);

  MDNode *pVersionMD = pDxilValidatorVersionMD->getOperand(0);
  IFTBOOL(pVersionMD->getNumOperands() == kDxilVersionNumFields, DXC_E_INCORRECT_DXIL_METADATA);

  Major = ConstMDToUint32(pVersionMD->getOperand(kDxilVersionMajorIdx));
  Minor = ConstMDToUint32(pVersionMD->getOperand(kDxilVersionMinorIdx));
}

//
// DXIL shader model.
//
void DxilMDHelper::EmitDxilShaderModel(const ShaderModel *pSM) {
  NamedMDNode *pShaderModelNamedMD = m_pModule->getNamedMetadata(kDxilShaderModelMDName);
  IFTBOOL(pShaderModelNamedMD == nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  pShaderModelNamedMD = m_pModule->getOrInsertNamedMetadata(kDxilShaderModelMDName);

  Metadata *MDVals[kDxilShaderModelNumFields];
  MDVals[kDxilShaderModelTypeIdx ] = MDString::get(m_Ctx, pSM->GetKindName().c_str());
  MDVals[kDxilShaderModelMajorIdx] = Uint32ToConstMD(pSM->GetMajor());
  MDVals[kDxilShaderModelMinorIdx] = Uint32ToConstMD(pSM->GetMinor());

  pShaderModelNamedMD->addOperand(MDNode::get(m_Ctx, MDVals));
}

void DxilMDHelper::LoadDxilShaderModel(const ShaderModel *&pSM) {
  NamedMDNode *pShaderModelNamedMD = m_pModule->getNamedMetadata(kDxilShaderModelMDName);
  IFTBOOL(pShaderModelNamedMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  IFTBOOL(pShaderModelNamedMD->getNumOperands() == 1, DXC_E_INCORRECT_DXIL_METADATA);

  MDNode *pShaderModelMD = pShaderModelNamedMD->getOperand(0);
  IFTBOOL(pShaderModelMD->getNumOperands() == kDxilShaderModelNumFields, DXC_E_INCORRECT_DXIL_METADATA);

  MDString *pShaderTypeMD = dyn_cast<MDString>(pShaderModelMD->getOperand(kDxilShaderModelTypeIdx));
  IFTBOOL(pShaderTypeMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  unsigned Major = ConstMDToUint32(pShaderModelMD->getOperand(kDxilShaderModelMajorIdx));
  unsigned Minor = ConstMDToUint32(pShaderModelMD->getOperand(kDxilShaderModelMinorIdx));
  string ShaderModelName = pShaderTypeMD->getString();
  ShaderModelName += "_" + std::to_string(Major) + "_" + std::to_string(Minor);
  pSM = ShaderModel::GetByName(ShaderModelName.c_str());
  if (!pSM->IsValidForDxil()) {
    char ErrorMsgTxt[40];
    StringCchPrintfA(ErrorMsgTxt, _countof(ErrorMsgTxt),
                     "Unknown shader model '%s'", ShaderModelName.c_str());
    string ErrorMsg(ErrorMsgTxt);
    throw hlsl::Exception(DXC_E_INCORRECT_DXIL_METADATA, ErrorMsg);
  }
}

//
// Entry points.
//
void DxilMDHelper::EmitDxilEntryPoints(vector<MDNode *> &MDEntries) {
  DXASSERT(MDEntries.size() == 1, "only one entry point is supported for now");
  NamedMDNode *pEntryPointsNamedMD = m_pModule->getNamedMetadata(kDxilEntryPointsMDName);
  IFTBOOL(pEntryPointsNamedMD == nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  pEntryPointsNamedMD = m_pModule->getOrInsertNamedMetadata(kDxilEntryPointsMDName);

  for (size_t i = 0; i < MDEntries.size(); i++) {
    pEntryPointsNamedMD->addOperand(MDEntries[i]);
  }
}

const NamedMDNode *DxilMDHelper::GetDxilEntryPoints() {
  NamedMDNode *pEntryPointsNamedMD = m_pModule->getNamedMetadata(kDxilEntryPointsMDName);
  IFTBOOL(pEntryPointsNamedMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);

  return pEntryPointsNamedMD;
}

MDTuple *DxilMDHelper::EmitDxilEntryPointTuple(Function *pFunc, const string &Name, 
                                               MDTuple *pSignatures, MDTuple *pResources,
                                               MDTuple *pProperties) {
  Metadata *MDVals[kDxilEntryPointNumFields];
  MDVals[kDxilEntryPointFunction  ] = pFunc ? ValueAsMetadata::get(pFunc) : nullptr;
  MDVals[kDxilEntryPointName      ] = MDString::get(m_Ctx, Name.c_str());
  MDVals[kDxilEntryPointSignatures] = pSignatures;
  MDVals[kDxilEntryPointResources ] = pResources;
  MDVals[kDxilEntryPointProperties] = pProperties;
  return MDNode::get(m_Ctx, MDVals);
}

void DxilMDHelper::GetDxilEntryPoint(const MDNode *MDO, Function *&pFunc, string &Name,
                                     const MDOperand *&pSignatures, const MDOperand *&pResources,
                                     const MDOperand *&pProperties) {
  IFTBOOL(MDO != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  const MDTuple *pTupleMD = dyn_cast<MDTuple>(MDO);
  IFTBOOL(pTupleMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  IFTBOOL(pTupleMD->getNumOperands() == kDxilEntryPointNumFields, DXC_E_INCORRECT_DXIL_METADATA);

  // Retrieve entry function symbol.
  const MDOperand &MDOFunc = pTupleMD->getOperand(kDxilEntryPointFunction);
  if (MDOFunc.get() != nullptr) {
    ValueAsMetadata *pValueFunc = dyn_cast<ValueAsMetadata>(MDOFunc.get());
    IFTBOOL(pValueFunc != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
    pFunc = dyn_cast<Function>(pValueFunc->getValue());
    IFTBOOL(pFunc != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  } else {
    pFunc = nullptr;  // pass-through CP.
  }

  // Retrieve entry function name.
  const MDOperand &MDOName = pTupleMD->getOperand(kDxilEntryPointName);
  IFTBOOL(MDOName.get() != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  MDString *pMDName = dyn_cast<MDString>(MDOName);
  IFTBOOL(pMDName != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  Name = pMDName->getString();

  pSignatures = &pTupleMD->getOperand(kDxilEntryPointSignatures);
  pResources  = &pTupleMD->getOperand(kDxilEntryPointResources );
  pProperties = &pTupleMD->getOperand(kDxilEntryPointProperties);
}

//
// Signatures.
//
MDTuple *DxilMDHelper::EmitDxilSignatures(const DxilSignature &InputSig, 
                                          const DxilSignature &OutputSig,
                                          const DxilSignature &PCSig) {
  MDTuple *pSignatureTupleMD = nullptr;

  if (!InputSig.GetElements().empty() || !OutputSig.GetElements().empty() || !PCSig.GetElements().empty()) {
    Metadata *MDVals[kDxilNumSignatureFields];
    MDVals[kDxilInputSignature]         = EmitSignatureMetadata(InputSig);
    MDVals[kDxilOutputSignature]        = EmitSignatureMetadata(OutputSig);
    MDVals[kDxilPatchConstantSignature] = EmitSignatureMetadata(PCSig);

    pSignatureTupleMD = MDNode::get(m_Ctx, MDVals);
  }

  return pSignatureTupleMD;
}

void DxilMDHelper::EmitRootSignature(RootSignatureHandle &RootSig) {
  if (RootSig.IsEmpty()) {
    return;
  }

  RootSig.EnsureSerializedAvailable();
  Constant *V = llvm::ConstantDataArray::get(
      m_Ctx, llvm::ArrayRef<uint8_t>(RootSig.GetSerializedBytes(),
                                     RootSig.GetSerializedSize()));

  NamedMDNode *pRootSignatureNamedMD = m_pModule->getNamedMetadata(kDxilRootSignatureMDName);
  IFTBOOL(pRootSignatureNamedMD == nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  pRootSignatureNamedMD = m_pModule->getOrInsertNamedMetadata(kDxilRootSignatureMDName);
  pRootSignatureNamedMD->addOperand(MDNode::get(m_Ctx, {ConstantAsMetadata::get(V)}));
  return ;
}

void DxilMDHelper::LoadDxilSignatures(const MDOperand &MDO, 
                                      DxilSignature &InputSig, 
                                      DxilSignature &OutputSig,
                                      DxilSignature &PCSig) {
  if (MDO.get() == nullptr)
    return;

  const MDTuple *pTupleMD = dyn_cast<MDTuple>(MDO.get());
  IFTBOOL(pTupleMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  IFTBOOL(pTupleMD->getNumOperands() == kDxilNumSignatureFields, DXC_E_INCORRECT_DXIL_METADATA);

  LoadSignatureMetadata(pTupleMD->getOperand(kDxilInputSignature),         InputSig);
  LoadSignatureMetadata(pTupleMD->getOperand(kDxilOutputSignature),        OutputSig);
  LoadSignatureMetadata(pTupleMD->getOperand(kDxilPatchConstantSignature), PCSig);
}

MDTuple *DxilMDHelper::EmitSignatureMetadata(const DxilSignature &Sig) {
  auto &Elements = Sig.GetElements();
  if (Elements.empty())
    return nullptr;

  vector<Metadata *> MDVals;
  for (size_t i = 0; i < Elements.size(); i++) {
    MDVals.emplace_back(EmitSignatureElement(*Elements[i]));
  }

  return MDNode::get(m_Ctx, MDVals);
}

void DxilMDHelper::LoadSignatureMetadata(const MDOperand &MDO, DxilSignature &Sig) {
  if (MDO.get() == nullptr)
    return;

  const MDTuple *pTupleMD = dyn_cast<MDTuple>(MDO.get());
  IFTBOOL(pTupleMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);

  for (unsigned i = 0; i < pTupleMD->getNumOperands(); i++) {
    unique_ptr<DxilSignatureElement> pSE(Sig.CreateElement());
    LoadSignatureElement(pTupleMD->getOperand(i), *pSE.get());
    Sig.AppendElement(std::move(pSE));
  }
}

void DxilMDHelper::LoadRootSignature(RootSignatureHandle &Sig) {
  NamedMDNode *pRootSignatureNamedMD = m_pModule->getNamedMetadata(kDxilRootSignatureMDName);
  if(!pRootSignatureNamedMD)
    return;

  IFTBOOL(pRootSignatureNamedMD->getNumOperands() == 1, DXC_E_INCORRECT_DXIL_METADATA);

  MDNode *pNode = pRootSignatureNamedMD->getOperand(0);
  IFTBOOL(pNode->getNumOperands() == 1, DXC_E_INCORRECT_DXIL_METADATA);
  const MDOperand &MDO = pNode->getOperand(0);

  const ConstantAsMetadata *pMetaData = dyn_cast<ConstantAsMetadata>(MDO.get());
  IFTBOOL(pMetaData != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  const ConstantDataArray *pData =
      dyn_cast<ConstantDataArray>(pMetaData->getValue());
  IFTBOOL(pData != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  IFTBOOL(pData->getElementType() == Type::getInt8Ty(m_Ctx),
          DXC_E_INCORRECT_DXIL_METADATA);

  Sig.Clear();
  Sig.LoadSerialized((uint8_t *)pData->getRawDataValues().begin(),
                     pData->getRawDataValues().size());
}

static const MDTuple *CastToTupleOrNull(const MDOperand &MDO) {
  if (MDO.get() == nullptr)
    return nullptr;

  const MDTuple *pTupleMD = dyn_cast<MDTuple>(MDO.get());
  IFTBOOL(pTupleMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  return pTupleMD;
}

MDTuple *DxilMDHelper::EmitSignatureElement(const DxilSignatureElement &SE) {
  Metadata *MDVals[kDxilSignatureElementNumFields];

  MDVals[kDxilSignatureElementID            ] = Uint32ToConstMD(SE.GetID());
  MDVals[kDxilSignatureElementName          ] = MDString::get(m_Ctx, SE.GetName());
  MDVals[kDxilSignatureElementType          ] = Uint8ToConstMD((uint8_t)SE.GetCompType().GetKind());
  MDVals[kDxilSignatureElementSystemValue   ] = Uint8ToConstMD((uint8_t)SE.GetKind());
  MDVals[kDxilSignatureElementIndexVector   ] = Uint32VectorToConstMDTuple(SE.GetSemanticIndexVec());
  MDVals[kDxilSignatureElementInterpMode    ] = Uint8ToConstMD((uint8_t)SE.GetInterpolationMode()->GetKind());
  MDVals[kDxilSignatureElementRows          ] = Uint32ToConstMD(SE.GetRows());
  MDVals[kDxilSignatureElementCols          ] = Uint8ToConstMD((uint8_t)SE.GetCols());
  MDVals[kDxilSignatureElementStartRow      ] = Int32ToConstMD(SE.GetStartRow());
  MDVals[kDxilSignatureElementStartCol      ] = Int8ToConstMD((int8_t)SE.GetStartCol());

  // Name-value list of extended properties.
  MDVals[kDxilSignatureElementNameValueList] = nullptr;
  vector<Metadata *> MDExtraVals;
  m_ExtraPropertyHelper->EmitSignatureElementProperties(SE, MDExtraVals);
  if (!MDExtraVals.empty()) {
    MDVals[kDxilSignatureElementNameValueList] = MDNode::get(m_Ctx, MDExtraVals);
  }

  // NOTE: when extra properties for signature elements are needed, extend ExtraPropertyHelper.

  return MDNode::get(m_Ctx, MDVals);
}

void DxilMDHelper::LoadSignatureElement(const MDOperand &MDO, DxilSignatureElement &SE) {
  IFTBOOL(MDO.get() != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  const MDTuple *pTupleMD = dyn_cast<MDTuple>(MDO.get());
  IFTBOOL(pTupleMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  IFTBOOL(pTupleMD->getNumOperands() == kDxilSignatureElementNumFields, DXC_E_INCORRECT_DXIL_METADATA);

  unsigned ID = ConstMDToUint32(                      pTupleMD->getOperand(kDxilSignatureElementID));
  MDString *pName = dyn_cast<MDString>(               pTupleMD->getOperand(kDxilSignatureElementName));
  CompType CT = CompType(ConstMDToUint8(              pTupleMD->getOperand(kDxilSignatureElementType)));
  DXIL::SemanticKind SemKind = 
    (DXIL::SemanticKind)ConstMDToUint8(               pTupleMD->getOperand(kDxilSignatureElementSystemValue));
  MDTuple *pSemanticIndexVectorMD = dyn_cast<MDTuple>(pTupleMD->getOperand(kDxilSignatureElementIndexVector));
  InterpolationMode IM(ConstMDToUint8(                pTupleMD->getOperand(kDxilSignatureElementInterpMode)));
  unsigned NumRows = ConstMDToUint32(                 pTupleMD->getOperand(kDxilSignatureElementRows));
  uint8_t NumCols = ConstMDToUint8(                   pTupleMD->getOperand(kDxilSignatureElementCols));
  int32_t StartRow = ConstMDToInt32(                  pTupleMD->getOperand(kDxilSignatureElementStartRow));
  int8_t StartCol = ConstMDToInt8(                    pTupleMD->getOperand(kDxilSignatureElementStartCol));

  IFTBOOL(pName != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  IFTBOOL(pSemanticIndexVectorMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);

  vector<unsigned> SemanticIndexVector;
  ConstMDTupleToUint32Vector(pSemanticIndexVectorMD, SemanticIndexVector);

  SE.Initialize(pName->getString(), CT, IM, NumRows, NumCols, StartRow, StartCol, ID, SemanticIndexVector);
  SE.SetKind(SemKind);

  // Name-value list of extended properties.
  m_ExtraPropertyHelper->LoadSignatureElementProperties(pTupleMD->getOperand(kDxilSignatureElementNameValueList), SE);
}

//
// Resources.
//
MDTuple *DxilMDHelper::EmitDxilResourceTuple(MDTuple *pSRVs, MDTuple *pUAVs, 
                                             MDTuple *pCBuffers, MDTuple *pSamplers) {
  DXASSERT(pSRVs != nullptr || pUAVs != nullptr || pCBuffers != nullptr || pSamplers != nullptr, "resource tuple should not be emitted if there are no resources");
  Metadata *MDVals[kDxilNumResourceFields];
  MDVals[kDxilResourceSRVs    ] = pSRVs;
  MDVals[kDxilResourceUAVs    ] = pUAVs;
  MDVals[kDxilResourceCBuffers] = pCBuffers;
  MDVals[kDxilResourceSamplers] = pSamplers;
  MDTuple *pTupleMD = MDNode::get(m_Ctx, MDVals);

  NamedMDNode *pResourcesNamedMD = m_pModule->getNamedMetadata(kDxilResourcesMDName);
  IFTBOOL(pResourcesNamedMD == nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  pResourcesNamedMD = m_pModule->getOrInsertNamedMetadata(kDxilResourcesMDName);
  pResourcesNamedMD->addOperand(pTupleMD);

  return pTupleMD;
}

void DxilMDHelper::GetDxilResources(const MDOperand &MDO, const MDTuple *&pSRVs,
                                    const MDTuple *&pUAVs, const MDTuple *&pCBuffers,
                                    const MDTuple *&pSamplers) {
  IFTBOOL(MDO.get() != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  const MDTuple *pTupleMD = dyn_cast<MDTuple>(MDO.get());
  IFTBOOL(pTupleMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  IFTBOOL(pTupleMD->getNumOperands() == kDxilNumResourceFields, DXC_E_INCORRECT_DXIL_METADATA);

  pSRVs     = CastToTupleOrNull(pTupleMD->getOperand(kDxilResourceSRVs    ));
  pUAVs     = CastToTupleOrNull(pTupleMD->getOperand(kDxilResourceUAVs    ));
  pCBuffers = CastToTupleOrNull(pTupleMD->getOperand(kDxilResourceCBuffers));
  pSamplers = CastToTupleOrNull(pTupleMD->getOperand(kDxilResourceSamplers));
}

void DxilMDHelper::EmitDxilResourceBase(const DxilResourceBase &R, Metadata *ppMDVals[]) {
  ppMDVals[kDxilResourceBaseID        ] = Uint32ToConstMD(R.GetID());
  ppMDVals[kDxilResourceBaseVariable  ] = ValueAsMetadata::get(R.GetGlobalSymbol());
  ppMDVals[kDxilResourceBaseName      ] = MDString::get(m_Ctx, R.GetGlobalName());
  ppMDVals[kDxilResourceBaseSpaceID   ] = Uint32ToConstMD(R.GetSpaceID());
  ppMDVals[kDxilResourceBaseLowerBound] = Uint32ToConstMD(R.GetLowerBound());
  ppMDVals[kDxilResourceBaseRangeSize ] = Uint32ToConstMD(R.GetRangeSize());
}

void DxilMDHelper::LoadDxilResourceBase(const MDOperand &MDO, DxilResourceBase &R) {
  IFTBOOL(MDO.get() != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  const MDTuple *pTupleMD = dyn_cast<MDTuple>(MDO.get());
  IFTBOOL(pTupleMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  IFTBOOL(pTupleMD->getNumOperands() >= kDxilResourceBaseNumFields, DXC_E_INCORRECT_DXIL_METADATA);

  R.SetID(ConstMDToUint32(pTupleMD->getOperand(kDxilResourceBaseID)));
  R.SetGlobalSymbol(dyn_cast<Constant>(ValueMDToValue(pTupleMD->getOperand(kDxilResourceBaseVariable))));
  R.SetGlobalName(StringMDToString(pTupleMD->getOperand(kDxilResourceBaseName)));
  R.SetSpaceID(ConstMDToUint32(pTupleMD->getOperand(kDxilResourceBaseSpaceID)));
  R.SetLowerBound(ConstMDToUint32(pTupleMD->getOperand(kDxilResourceBaseLowerBound)));
  R.SetRangeSize(ConstMDToUint32(pTupleMD->getOperand(kDxilResourceBaseRangeSize)));
}

MDTuple *DxilMDHelper::EmitDxilSRV(const DxilResource &SRV) {
  Metadata *MDVals[kDxilSRVNumFields];

  EmitDxilResourceBase(SRV, &MDVals[0]);

  // SRV-specific fields.
  MDVals[kDxilSRVShape        ] = Uint32ToConstMD((unsigned)SRV.GetKind());
  MDVals[kDxilSRVSampleCount  ] = Uint32ToConstMD(SRV.GetSampleCount());

  // Name-value list of extended properties.
  MDVals[kDxilSRVNameValueList] = nullptr;
  vector<Metadata *> MDExtraVals;
  m_ExtraPropertyHelper->EmitSRVProperties(SRV, MDExtraVals);
  if (!MDExtraVals.empty()) {
    MDVals[kDxilSRVNameValueList] = MDNode::get(m_Ctx, MDExtraVals);
  }

  return MDNode::get(m_Ctx, MDVals);
}

void DxilMDHelper::LoadDxilSRV(const MDOperand &MDO, DxilResource &SRV) {
  IFTBOOL(MDO.get() != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  const MDTuple *pTupleMD = dyn_cast<MDTuple>(MDO.get());
  IFTBOOL(pTupleMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  IFTBOOL(pTupleMD->getNumOperands() == kDxilSRVNumFields, DXC_E_INCORRECT_DXIL_METADATA);

  SRV.SetRW(false);

  LoadDxilResourceBase(MDO, SRV);

  // SRV-specific fields.
  SRV.SetKind((DxilResource::Kind)ConstMDToUint32(pTupleMD->getOperand(kDxilSRVShape)));
  SRV.SetSampleCount(ConstMDToUint32(pTupleMD->getOperand(kDxilSRVSampleCount)));

  // Name-value list of extended properties.
  m_ExtraPropertyHelper->LoadSRVProperties(pTupleMD->getOperand(kDxilSRVNameValueList), SRV);
}

MDTuple *DxilMDHelper::EmitDxilUAV(const DxilResource &UAV) {
  Metadata *MDVals[kDxilUAVNumFields];

  EmitDxilResourceBase(UAV, &MDVals[0]);

  // UAV-specific fields.
  MDVals[kDxilUAVShape                ] = Uint32ToConstMD((unsigned)UAV.GetKind());
  MDVals[kDxilUAVGloballyCoherent     ] = BoolToConstMD(UAV.IsGloballyCoherent());
  MDVals[kDxilUAVCounter              ] = BoolToConstMD(UAV.HasCounter());
  MDVals[kDxilUAVRasterizerOrderedView] = BoolToConstMD(UAV.IsROV());

  // Name-value list of extended properties.
  MDVals[kDxilUAVNameValueList        ] = nullptr;
  vector<Metadata *> MDExtraVals;
  m_ExtraPropertyHelper->EmitUAVProperties(UAV, MDExtraVals);
  if (!MDExtraVals.empty()) {
    MDVals[kDxilUAVNameValueList] = MDNode::get(m_Ctx, MDExtraVals);
  }

  return MDNode::get(m_Ctx, MDVals);
}

void DxilMDHelper::LoadDxilUAV(const MDOperand &MDO, DxilResource &UAV) {
  IFTBOOL(MDO.get() != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  const MDTuple *pTupleMD = dyn_cast<MDTuple>(MDO.get());
  IFTBOOL(pTupleMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  IFTBOOL(pTupleMD->getNumOperands() == kDxilUAVNumFields, DXC_E_INCORRECT_DXIL_METADATA);

  UAV.SetRW(true);

  LoadDxilResourceBase(MDO, UAV);

  // UAV-specific fields.
  UAV.SetKind((DxilResource::Kind)ConstMDToUint32(pTupleMD->getOperand(kDxilUAVShape)));
  UAV.SetGloballyCoherent(ConstMDToBool(pTupleMD->getOperand(kDxilUAVGloballyCoherent)));
  UAV.SetHasCounter(ConstMDToBool(pTupleMD->getOperand(kDxilUAVCounter)));
  UAV.SetROV(ConstMDToBool(pTupleMD->getOperand(kDxilUAVRasterizerOrderedView)));

  // Name-value list of extended properties.
  m_ExtraPropertyHelper->LoadUAVProperties(pTupleMD->getOperand(kDxilUAVNameValueList), UAV);
}

MDTuple *DxilMDHelper::EmitDxilCBuffer(const DxilCBuffer &CB) {
  Metadata *MDVals[kDxilCBufferNumFields];

  EmitDxilResourceBase(CB, &MDVals[0]);

  // CBuffer-specific fields.
  // CBuffer size in bytes.
  MDVals[kDxilCBufferSizeInBytes  ] = Uint32ToConstMD(CB.GetSize());

  // Name-value list of extended properties.
  MDVals[kDxilCBufferNameValueList] = nullptr;
  vector<Metadata *> MDExtraVals;
  m_ExtraPropertyHelper->EmitCBufferProperties(CB, MDExtraVals);
  if (!MDExtraVals.empty()) {
    MDVals[kDxilCBufferNameValueList] = MDNode::get(m_Ctx, MDExtraVals);
  }

  return MDNode::get(m_Ctx, MDVals);
}

void DxilMDHelper::LoadDxilCBuffer(const MDOperand &MDO, DxilCBuffer &CB) {
  IFTBOOL(MDO.get() != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  const MDTuple *pTupleMD = dyn_cast<MDTuple>(MDO.get());
  IFTBOOL(pTupleMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  IFTBOOL(pTupleMD->getNumOperands() == kDxilCBufferNumFields, DXC_E_INCORRECT_DXIL_METADATA);

  LoadDxilResourceBase(MDO, CB);

  // CBuffer-specific fields.
  CB.SetSize(ConstMDToUint32(pTupleMD->getOperand(kDxilCBufferSizeInBytes)));

  // Name-value list of extended properties.
  m_ExtraPropertyHelper->LoadCBufferProperties(pTupleMD->getOperand(kDxilCBufferNameValueList), CB);
}

void DxilMDHelper::EmitDxilTypeSystem(DxilTypeSystem &TypeSystem, vector<GlobalVariable*> &LLVMUsed) {
  auto &TypeMap = TypeSystem.GetStructAnnotationMap();
  vector<Metadata *> MDVals;
  MDVals.emplace_back(Uint32ToConstMD(kDxilTypeSystemStructTag)); // Tag
  unsigned GVIdx = 0;
  for (auto it = TypeMap.begin(); it != TypeMap.end(); ++it, GVIdx++) {
    StructType *pStructType = const_cast<StructType *>(it->first);
    DxilStructAnnotation *pA = it->second.get();

    // Emit struct type field annotations.
    Metadata *pMD = EmitDxilStructAnnotation(*pA);

    MDVals.push_back(ValueAsMetadata::get(UndefValue::get(pStructType)));
    MDVals.push_back(pMD);
  }

  auto &FuncMap = TypeSystem.GetFunctionAnnotationMap();
  vector<Metadata *> MDFuncVals;
  MDFuncVals.emplace_back(Uint32ToConstMD(kDxilTypeSystemFunctionTag)); // Tag

  for (auto it = FuncMap.begin(); it != FuncMap.end(); ++it) {
    DxilFunctionAnnotation *pA = it->second.get();
    MDFuncVals.push_back(ValueAsMetadata::get(const_cast<Function*>(pA->GetFunction())));
    // Emit function annotations.
    Metadata *pMD = EmitDxilFunctionAnnotation(*pA);

    MDFuncVals.push_back(pMD);
  }

  if (MDVals.size() > 1) {
    NamedMDNode *pDxilTypeAnnotationsMD = m_pModule->getNamedMetadata(kDxilTypeSystemMDName);
    IFTBOOL(pDxilTypeAnnotationsMD == nullptr, DXC_E_INCORRECT_DXIL_METADATA);
    pDxilTypeAnnotationsMD = m_pModule->getOrInsertNamedMetadata(kDxilTypeSystemMDName);

    pDxilTypeAnnotationsMD->addOperand(MDNode::get(m_Ctx, MDVals));
  }
  if (MDFuncVals.size() > 1) {
    NamedMDNode *pDxilTypeAnnotationsMD = m_pModule->getNamedMetadata(kDxilTypeSystemMDName);
    if (pDxilTypeAnnotationsMD == nullptr)
      pDxilTypeAnnotationsMD = m_pModule->getOrInsertNamedMetadata(kDxilTypeSystemMDName);

    pDxilTypeAnnotationsMD->addOperand(MDNode::get(m_Ctx, MDFuncVals));
  }
}

void DxilMDHelper::LoadDxilTypeSystemNode(const llvm::MDTuple &MDT,
                                          DxilTypeSystem &TypeSystem) {

  unsigned Tag = ConstMDToUint32(MDT.getOperand(0));
  if (Tag == kDxilTypeSystemStructTag) {
    IFTBOOL((MDT.getNumOperands() & 0x1) == 1, DXC_E_INCORRECT_DXIL_METADATA);

    for (unsigned i = 1; i < MDT.getNumOperands(); i += 2) {
      Constant *pGV =
          dyn_cast<Constant>(ValueMDToValue(MDT.getOperand(i)));
      IFTBOOL(pGV != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
      StructType *pGVType =
          dyn_cast<StructType>(pGV->getType());
      IFTBOOL(pGVType != nullptr, DXC_E_INCORRECT_DXIL_METADATA);

      DxilStructAnnotation *pSA = TypeSystem.AddStructAnnotation(pGVType);
      LoadDxilStructAnnotation(MDT.getOperand(i + 1), *pSA);
    }
  } else {
    IFTBOOL(Tag == kDxilTypeSystemFunctionTag, DXC_E_INCORRECT_DXIL_METADATA);
    IFTBOOL((MDT.getNumOperands() & 0x1) == 1, DXC_E_INCORRECT_DXIL_METADATA);
    for (unsigned i = 1; i < MDT.getNumOperands(); i += 2) {
      Function *F = dyn_cast<Function>(ValueMDToValue(MDT.getOperand(i)));
      DxilFunctionAnnotation *pFA = TypeSystem.AddFunctionAnnotation(F);
      LoadDxilFunctionAnnotation(MDT.getOperand(i + 1), *pFA);
    }
  }
}

void DxilMDHelper::LoadDxilTypeSystem(DxilTypeSystem &TypeSystem) {
  NamedMDNode *pDxilTypeAnnotationsMD = m_pModule->getNamedMetadata(kDxilTypeSystemMDName);
  if (pDxilTypeAnnotationsMD == nullptr)
    return;

  IFTBOOL(pDxilTypeAnnotationsMD->getNumOperands() <= 2, DXC_E_INCORRECT_DXIL_METADATA);
  for (unsigned i = 0; i < pDxilTypeAnnotationsMD->getNumOperands(); i++) {
    const MDTuple *pTupleMD = dyn_cast<MDTuple>(pDxilTypeAnnotationsMD->getOperand(i));
    IFTBOOL(pTupleMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
    LoadDxilTypeSystemNode(*pTupleMD, TypeSystem);
  }
}

Metadata *DxilMDHelper::EmitDxilStructAnnotation(const DxilStructAnnotation &SA) {
  vector<Metadata *> MDVals(SA.GetNumFields() + 1);
  MDVals[0] = Uint32ToConstMD(SA.GetCBufferSize());
  for (unsigned i = 0; i < SA.GetNumFields(); i++) {
    MDVals[i+1] = EmitDxilFieldAnnotation(SA.GetFieldAnnotation(i));
  }

  return MDNode::get(m_Ctx, MDVals);
}

void DxilMDHelper::LoadDxilStructAnnotation(const MDOperand &MDO, DxilStructAnnotation &SA) {
  IFTBOOL(MDO.get() != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  const MDTuple *pTupleMD = dyn_cast<MDTuple>(MDO.get());
  IFTBOOL(pTupleMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  if (pTupleMD->getNumOperands() == 1) {
    const StructType *ST = SA.GetStructType();
    if (ST->getNumElements() == 1) {
      Type *EltTy = ST->getElementType(0);
      if (EltTy == Type::getInt8Ty(ST->getContext())) {
        SA.MarkEmptyStruct();
      }
    }
  }
  IFTBOOL(pTupleMD->getNumOperands() == SA.GetNumFields()+1, DXC_E_INCORRECT_DXIL_METADATA);

  SA.SetCBufferSize(ConstMDToUint32(pTupleMD->getOperand(0)));
  for (unsigned i = 0; i < SA.GetNumFields(); i++) {
    const MDOperand &MDO = pTupleMD->getOperand(i+1);
    DxilFieldAnnotation &FA = SA.GetFieldAnnotation(i);
    LoadDxilFieldAnnotation(MDO, FA);
  }
}

Metadata *
DxilMDHelper::EmitDxilFunctionAnnotation(const DxilFunctionAnnotation &FA) {
  vector<Metadata *> MDVals(FA.GetNumParameters() + 1);
  MDVals[0] = EmitDxilParamAnnotation(FA.GetRetTypeAnnotation());
  for (unsigned i = 0; i < FA.GetNumParameters(); i++) {
    MDVals[i + 1] = EmitDxilParamAnnotation(FA.GetParameterAnnotation(i));
  }

  return MDNode::get(m_Ctx, MDVals);
}
void DxilMDHelper::LoadDxilFunctionAnnotation(const MDOperand &MDO,
                                              DxilFunctionAnnotation &FA) {
  IFTBOOL(MDO.get() != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  const MDTuple *pTupleMD = dyn_cast<MDTuple>(MDO.get());
  IFTBOOL(pTupleMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  IFTBOOL(pTupleMD->getNumOperands() == FA.GetNumParameters() + 1,
          DXC_E_INCORRECT_DXIL_METADATA);

  DxilParameterAnnotation &retTyAnnotation = FA.GetRetTypeAnnotation();
  LoadDxilParamAnnotation(pTupleMD->getOperand(0), retTyAnnotation);
  for (unsigned i = 0; i < FA.GetNumParameters(); i++) {
    const MDOperand &MDO = pTupleMD->getOperand(i + 1);
    DxilParameterAnnotation &PA = FA.GetParameterAnnotation(i);
    LoadDxilParamAnnotation(MDO, PA);
  }
}
Metadata *
DxilMDHelper::EmitDxilParamAnnotation(const DxilParameterAnnotation &PA) {
  vector<Metadata *> MDVals(3);
  MDVals[0] = Uint32ToConstMD(static_cast<unsigned>(PA.GetParamInputQual()));
  MDVals[1] = EmitDxilFieldAnnotation(PA);
  MDVals[2] = Uint32VectorToConstMDTuple(PA.GetSemanticIndexVec());

  return MDNode::get(m_Ctx, MDVals);
}
void DxilMDHelper::LoadDxilParamAnnotation(const MDOperand &MDO,
                                           DxilParameterAnnotation &PA) {
  IFTBOOL(MDO.get() != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  const MDTuple *pTupleMD = dyn_cast<MDTuple>(MDO.get());
  IFTBOOL(pTupleMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  IFTBOOL(pTupleMD->getNumOperands() == 3, DXC_E_INCORRECT_DXIL_METADATA);
  PA.SetParamInputQual(static_cast<DxilParamInputQual>(
      ConstMDToUint32(pTupleMD->getOperand(0))));
  LoadDxilFieldAnnotation(pTupleMD->getOperand(1), PA);
  MDTuple *pSemanticIndexVectorMD = dyn_cast<MDTuple>(pTupleMD->getOperand(2));
  vector<unsigned> SemanticIndexVector;
  ConstMDTupleToUint32Vector(pSemanticIndexVectorMD, SemanticIndexVector);
  PA.SetSemanticIndexVec(SemanticIndexVector);
}

Metadata *DxilMDHelper::EmitDxilFieldAnnotation(const DxilFieldAnnotation &FA) {
  vector<Metadata *> MDVals;  // Tag-Value list.

  if (FA.HasFieldName()) {
    MDVals.emplace_back(Uint32ToConstMD(kDxilFieldAnnotationFieldNameTag));
    MDVals.emplace_back(MDString::get(m_Ctx, FA.GetFieldName()));
  }
  if (FA.IsPrecise()) {
    MDVals.emplace_back(Uint32ToConstMD(kDxilFieldAnnotationPreciseTag)); // Tag
    MDVals.emplace_back(BoolToConstMD(true));                             // Value
  }
  if (FA.HasMatrixAnnotation()) {
    const DxilMatrixAnnotation &MA = FA.GetMatrixAnnotation();
    Metadata *MatrixMD[3];
    MatrixMD[0] = Uint32ToConstMD(MA.Rows);
    MatrixMD[1] = Uint32ToConstMD(MA.Cols);
    MatrixMD[2] = Uint32ToConstMD((unsigned)MA.Orientation);

    MDVals.emplace_back(Uint32ToConstMD(kDxilFieldAnnotationMatrixTag));
    MDVals.emplace_back(MDNode::get(m_Ctx, MatrixMD));
  }
  if (FA.HasCBufferOffset()) {
    MDVals.emplace_back(Uint32ToConstMD(kDxilFieldAnnotationCBufferOffsetTag));
    MDVals.emplace_back(Uint32ToConstMD(FA.GetCBufferOffset()));
  }
  if (FA.HasSemanticString()) {
    MDVals.emplace_back(Uint32ToConstMD(kDxilFieldAnnotationSemanticStringTag));
    MDVals.emplace_back(MDString::get(m_Ctx, FA.GetSemanticString()));
  }
  if (FA.HasInterpolationMode()) {
    MDVals.emplace_back(Uint32ToConstMD(kDxilFieldAnnotationInterpolationModeTag));
    MDVals.emplace_back(Uint32ToConstMD((unsigned)FA.GetInterpolationMode().GetKind()));
  }
  if (FA.HasCompType()) {
    MDVals.emplace_back(Uint32ToConstMD(kDxilFieldAnnotationCompTypeTag));
    MDVals.emplace_back(Uint32ToConstMD((unsigned)FA.GetCompType().GetKind()));
  }

  return MDNode::get(m_Ctx, MDVals);
}

void DxilMDHelper::LoadDxilFieldAnnotation(const MDOperand &MDO, DxilFieldAnnotation &FA) {
  IFTBOOL(MDO.get() != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  const MDTuple *pTupleMD = dyn_cast<MDTuple>(MDO.get());
  IFTBOOL(pTupleMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  IFTBOOL((pTupleMD->getNumOperands() & 0x1) == 0, DXC_E_INCORRECT_DXIL_METADATA);

  for (unsigned i = 0; i < pTupleMD->getNumOperands(); i += 2) {
    unsigned Tag = ConstMDToUint32(pTupleMD->getOperand(i));
    const MDOperand &MDO = pTupleMD->getOperand(i + 1);
    IFTBOOL(MDO.get() != nullptr, DXC_E_INCORRECT_DXIL_METADATA);

    switch (Tag) {
    case kDxilFieldAnnotationPreciseTag:
      FA.SetPrecise(ConstMDToBool(MDO));
      break;
    case kDxilFieldAnnotationMatrixTag: {
      DxilMatrixAnnotation MA;
      const MDTuple *pMATupleMD = dyn_cast<MDTuple>(MDO.get());
      IFTBOOL(pMATupleMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
      IFTBOOL(pMATupleMD->getNumOperands() == 3, DXC_E_INCORRECT_DXIL_METADATA);
      MA.Rows = ConstMDToUint32(pMATupleMD->getOperand(0));
      MA.Cols = ConstMDToUint32(pMATupleMD->getOperand(1));
      MA.Orientation = (MatrixOrientation)ConstMDToUint32(pMATupleMD->getOperand(2));
      FA.SetMatrixAnnotation(MA);
    } break;
    case kDxilFieldAnnotationCBufferOffsetTag:
      FA.SetCBufferOffset(ConstMDToUint32(MDO));
      break;
    case kDxilFieldAnnotationSemanticStringTag:
      FA.SetSemanticString(StringMDToString(MDO));
      break;
    case kDxilFieldAnnotationInterpolationModeTag:
      FA.SetInterpolationMode(InterpolationMode((InterpolationMode::Kind)ConstMDToUint32(MDO)));
      break;
    case kDxilFieldAnnotationFieldNameTag:
      FA.SetFieldName(StringMDToString(MDO));
      break;
    case kDxilFieldAnnotationCompTypeTag:
      FA.SetCompType((CompType::Kind)ConstMDToUint32(MDO));
      break;
    default:
      // TODO:  I don't think we should be failing unrecognized extended tags.
      //        Perhaps we can flag this case in the module and fail validation
      //        if flagged.
      //        That way, an existing loader will not fail on an additional tag
      //        and the blob would not be signed if the extra tag was not legal.
      IFTBOOL(false, DXC_E_INCORRECT_DXIL_METADATA);
    }
  }
}

void DxilMDHelper::EmitDxilViewIdState(DxilViewIdState &ViewIdState) {
  const vector<unsigned> &Data = ViewIdState.GetSerialized();
  // If all UINTs are zero, do not emit ViewIdState.
  if (!std::any_of(Data.begin(), Data.end(), [](unsigned e){return e!=0;}))
    return;

  Constant *V = ConstantDataArray::get(m_Ctx, ArrayRef<uint32_t>(Data));
  NamedMDNode *pViewIdNamedMD = m_pModule->getNamedMetadata(kDxilViewIdStateMDName);
  IFTBOOL(pViewIdNamedMD == nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  pViewIdNamedMD = m_pModule->getOrInsertNamedMetadata(kDxilViewIdStateMDName);
  pViewIdNamedMD->addOperand(MDNode::get(m_Ctx, {ConstantAsMetadata::get(V)}));
}

void DxilMDHelper::LoadDxilViewIdState(DxilViewIdState &ViewIdState) {
  NamedMDNode *pViewIdStateNamedMD = m_pModule->getNamedMetadata(kDxilViewIdStateMDName);
  if(!pViewIdStateNamedMD)
    return;

  IFTBOOL(pViewIdStateNamedMD->getNumOperands() == 1, DXC_E_INCORRECT_DXIL_METADATA);

  MDNode *pNode = pViewIdStateNamedMD->getOperand(0);
  IFTBOOL(pNode->getNumOperands() == 1, DXC_E_INCORRECT_DXIL_METADATA);
  const MDOperand &MDO = pNode->getOperand(0);

  const ConstantAsMetadata *pMetaData = dyn_cast<ConstantAsMetadata>(MDO.get());
  IFTBOOL(pMetaData != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  if (isa<ConstantAggregateZero>(pMetaData->getValue()))
    return;
  const ConstantDataArray *pData = dyn_cast<ConstantDataArray>(pMetaData->getValue());
  IFTBOOL(pData != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  IFTBOOL(pData->getElementType() == Type::getInt32Ty(m_Ctx), DXC_E_INCORRECT_DXIL_METADATA);
  IFTBOOL(pData->getRawDataValues().size() < UINT_MAX && 
          (pData->getRawDataValues().size() & 3) == 0, DXC_E_INCORRECT_DXIL_METADATA);

  ViewIdState.Deserialize((unsigned *)pData->getRawDataValues().begin(), 
                          (unsigned)pData->getRawDataValues().size() / 4);
}

MDNode *DxilMDHelper::EmitControlFlowHints(llvm::LLVMContext &Ctx, std::vector<DXIL::ControlFlowHint> &hints) {
  SmallVector<Metadata *, 4> Args;
  // Reserve operand 0 for self reference.
  auto TempNode = MDNode::getTemporary(Ctx, None);
  Args.emplace_back(TempNode.get());
  Args.emplace_back(MDString::get(Ctx, kDxilControlFlowHintMDName));
  for (DXIL::ControlFlowHint &hint : hints)
    Args.emplace_back(Uint32ToConstMD(static_cast<unsigned>(hint), Ctx));

  MDNode *hintsNode = MDNode::get(Ctx, Args);
  // Set the first operand to itself.
  hintsNode->replaceOperandWith(0, hintsNode);
  return hintsNode;
}

void DxilMDHelper::EmitDxilPreciseMD() {
  MDNode *pDXPreciseMD = MDNode::get(m_pModule->getContext(), ConstantAsMetadata::get(ConstantInt::get(Type::getInt32Ty(m_pModule->getContext()), 1)));

  for (auto itF = m_pModule->begin(), endF = m_pModule->end(); itF != endF; ++itF) {
    Function *F = itF;
    
    for (auto itBB = F->begin(), endBB = F->end(); itBB != endBB; ++itBB) {
      BasicBlock *BB = itBB;

      for (auto itI = BB->begin(), endI = BB->end(); itI != endI; ++itI) {
        if (isa<FPMathOperator>(itI)) {
          if (CallInst *CI = dyn_cast<CallInst>(itI)) {
            MDNode *pMD = nullptr;
            FastMathFlags FMF = CI->getFastMathFlags();
            if (!FMF.unsafeAlgebra()) {
              pMD = pDXPreciseMD;
            } else {
              CI->copyFastMathFlags(FastMathFlags{});
            }
            CI->setMetadata(DxilMDHelper::kDxilPreciseAttributeMDName, pMD);
          }
        }
      }
    }
  }
}

void DxilMDHelper::LoadDxilPreciseMD() {
  if (!m_pModule->GetFromBitCode())
    return;

  FastMathFlags FMF;
  FMF.setUnsafeAlgebra();

  for (auto itF = m_pModule->begin(), endF = m_pModule->end(); itF != endF; ++itF) {
    Function *F = itF;
    
    for (auto itBB = F->begin(), endBB = F->end(); itBB != endBB; ++itBB) {
      BasicBlock *BB = itBB;

      for (auto itI = BB->begin(), endI = BB->end(); itI != endI; ++itI) {
        if (isa<FPMathOperator>(itI)) {
          if (CallInst *CI = dyn_cast<CallInst>(itI)) {
            MDNode *pMD = CI->getMetadata(DxilMDHelper::kDxilPreciseAttributeMDName);
            if (!pMD) {
              CI->copyFastMathFlags(FMF);
            } else {
              CI->setMetadata(DxilMDHelper::kDxilPreciseAttributeMDName, nullptr);
            }
          }
        }
      }
    }
  }
}

MDTuple *DxilMDHelper::EmitDxilSampler(const DxilSampler &S) {
  Metadata *MDVals[kDxilSamplerNumFields];

  EmitDxilResourceBase(S, &MDVals[0]);

  // Sampler-specific fields.
  MDVals[kDxilSamplerType         ] = Uint32ToConstMD((unsigned)S.GetSamplerKind());

  // Name-value list of extended properties.
  MDVals[kDxilSamplerNameValueList] = nullptr;
  vector<Metadata *> MDExtraVals;
  m_ExtraPropertyHelper->EmitSamplerProperties(S, MDExtraVals);
  if (!MDExtraVals.empty()) {
    MDVals[kDxilSamplerNameValueList] = MDNode::get(m_Ctx, MDExtraVals);
  }

  return MDNode::get(m_Ctx, MDVals);
}

void DxilMDHelper::LoadDxilSampler(const MDOperand &MDO, DxilSampler &S) {
  IFTBOOL(MDO.get() != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  const MDTuple *pTupleMD = dyn_cast<MDTuple>(MDO.get());
  IFTBOOL(pTupleMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  IFTBOOL(pTupleMD->getNumOperands() == kDxilSamplerNumFields, DXC_E_INCORRECT_DXIL_METADATA);

  LoadDxilResourceBase(MDO, S);

  // Sampler-specific fields.
  S.SetSamplerKind((DxilSampler::SamplerKind)ConstMDToUint32(pTupleMD->getOperand(kDxilSamplerType)));

  // Name-value list of extended properties.
  m_ExtraPropertyHelper->LoadSamplerProperties(pTupleMD->getOperand(kDxilSamplerNameValueList), S);
}

const MDOperand &DxilMDHelper::GetResourceClass(llvm::MDNode *MD,
                                                DXIL::ResourceClass &RC) {
  IFTBOOL(MD->getNumOperands() >=
              DxilMDHelper::kHLDxilResourceAttributeNumFields,
          DXC_E_INCORRECT_DXIL_METADATA);
  RC = static_cast<DxilResource::Class>(ConstMDToUint32(
      MD->getOperand(DxilMDHelper::kHLDxilResourceAttributeClass)));
  return MD->getOperand(DxilMDHelper::kHLDxilResourceAttributeMeta);
}

void DxilMDHelper::LoadDxilResourceBaseFromMDNode(llvm::MDNode *MD,
                                                  DxilResourceBase &R) {
  DxilResource::Class RC = DxilResource::Class::Invalid;
  const MDOperand &Meta = GetResourceClass(MD, RC);

  switch (RC) {
  case DxilResource::Class::CBuffer: {
    DxilCBuffer CB;
    LoadDxilCBuffer(Meta, CB);
    R = CB;
  } break;
  case DxilResource::Class::Sampler: {
    DxilSampler S;
    LoadDxilSampler(Meta, S);
    R = S;
  } break;
  case DxilResource::Class::SRV: {
    DxilResource Res;
    LoadDxilSRV(Meta, Res);
    R = Res;
  } break;
  case DxilResource::Class::UAV: {
    DxilResource Res;
    LoadDxilUAV(Meta, Res);
    R = Res;
  } break;
  default:
    DXASSERT(0, "Invalid metadata");
  }
}

void DxilMDHelper::LoadDxilResourceFromMDNode(llvm::MDNode *MD,
                                              DxilResource &R) {
  DxilResource::Class RC = DxilResource::Class::Invalid;
  const MDOperand &Meta = GetResourceClass(MD, RC);

  switch (RC) {
  case DxilResource::Class::SRV: {
    LoadDxilSRV(Meta, R);
  } break;
  case DxilResource::Class::UAV: {
    LoadDxilUAV(Meta, R);
  } break;
  default:
    DXASSERT(0, "Invalid metadata");
  }
}

void DxilMDHelper::LoadDxilSamplerFromMDNode(llvm::MDNode *MD, DxilSampler &S) {
  DxilResource::Class RC = DxilResource::Class::Invalid;
  const MDOperand &Meta = GetResourceClass(MD, RC);

  switch (RC) {
  case DxilResource::Class::Sampler: {
    LoadDxilSampler(Meta, S);
  } break;
  default:
    DXASSERT(0, "Invalid metadata");
  }
}

//
// DxilExtraPropertyHelper shader-specific methods.
//
MDTuple *DxilMDHelper::EmitDxilGSState(DXIL::InputPrimitive Primitive, 
                                       unsigned MaxVertexCount, 
                                       unsigned ActiveStreamMask, 
                                       DXIL::PrimitiveTopology StreamPrimitiveTopology, 
                                       unsigned GSInstanceCount) {
  Metadata *MDVals[kDxilGSStateNumFields];

  MDVals[kDxilGSStateInputPrimitive      ] = Uint32ToConstMD((unsigned)Primitive);
  MDVals[kDxilGSStateMaxVertexCount      ] = Uint32ToConstMD(MaxVertexCount);
  MDVals[kDxilGSStateActiveStreamMask    ] = Uint32ToConstMD(ActiveStreamMask);
  MDVals[kDxilGSStateOutputStreamTopology] = Uint32ToConstMD((unsigned)StreamPrimitiveTopology);
  MDVals[kDxilGSStateGSInstanceCount     ] = Uint32ToConstMD(GSInstanceCount);

  return MDNode::get(m_Ctx, MDVals);
}

void DxilMDHelper::LoadDxilGSState(const MDOperand &MDO, 
                                   DXIL::InputPrimitive &Primitive,
                                   unsigned &MaxVertexCount, 
                                   unsigned &ActiveStreamMask,
                                   DXIL::PrimitiveTopology &StreamPrimitiveTopology,
                                   unsigned &GSInstanceCount) {
  IFTBOOL(MDO.get() != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  const MDTuple *pTupleMD = dyn_cast<MDTuple>(MDO.get());
  IFTBOOL(pTupleMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  IFTBOOL(pTupleMD->getNumOperands() == kDxilGSStateNumFields, DXC_E_INCORRECT_DXIL_METADATA);

  Primitive = (DXIL::InputPrimitive)ConstMDToUint32(pTupleMD->getOperand(kDxilGSStateInputPrimitive));
  MaxVertexCount = ConstMDToUint32(pTupleMD->getOperand(kDxilGSStateMaxVertexCount));
  ActiveStreamMask = ConstMDToUint32(pTupleMD->getOperand(kDxilGSStateActiveStreamMask));
  StreamPrimitiveTopology = (DXIL::PrimitiveTopology)ConstMDToUint32(pTupleMD->getOperand(kDxilGSStateOutputStreamTopology));
  GSInstanceCount = ConstMDToUint32(pTupleMD->getOperand(kDxilGSStateGSInstanceCount));
}

MDTuple *DxilMDHelper::EmitDxilDSState(DXIL::TessellatorDomain Domain, unsigned InputControlPointCount) {
  Metadata *MDVals[kDxilDSStateNumFields];

  MDVals[kDxilDSStateTessellatorDomain     ] = Uint32ToConstMD((unsigned)Domain);
  MDVals[kDxilDSStateInputControlPointCount] = Uint32ToConstMD(InputControlPointCount);

  return MDNode::get(m_Ctx, MDVals);
}

void DxilMDHelper::LoadDxilDSState(const MDOperand &MDO,
                                   DXIL::TessellatorDomain &Domain,
                                   unsigned &InputControlPointCount) {
  IFTBOOL(MDO.get() != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  const MDTuple *pTupleMD = dyn_cast<MDTuple>(MDO.get());
  IFTBOOL(pTupleMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  IFTBOOL(pTupleMD->getNumOperands() == kDxilDSStateNumFields, DXC_E_INCORRECT_DXIL_METADATA);

  Domain = (DXIL::TessellatorDomain)ConstMDToUint32(pTupleMD->getOperand(kDxilDSStateTessellatorDomain));
  InputControlPointCount = ConstMDToUint32(pTupleMD->getOperand(kDxilDSStateInputControlPointCount));
}

MDTuple *DxilMDHelper::EmitDxilHSState(Function *pPatchConstantFunction,
                                       unsigned InputControlPointCount,
                                       unsigned OutputControlPointCount,
                                       DXIL::TessellatorDomain TessDomain,
                                       DXIL::TessellatorPartitioning TessPartitioning,
                                       DXIL::TessellatorOutputPrimitive TessOutputPrimitive,
                                       float MaxTessFactor) {
  Metadata *MDVals[kDxilHSStateNumFields];

  MDVals[kDxilHSStatePatchConstantFunction     ] = ValueAsMetadata::get(pPatchConstantFunction);
  MDVals[kDxilHSStateInputControlPointCount    ] = Uint32ToConstMD(InputControlPointCount);
  MDVals[kDxilHSStateOutputControlPointCount   ] = Uint32ToConstMD(OutputControlPointCount);
  MDVals[kDxilHSStateTessellatorDomain         ] = Uint32ToConstMD((unsigned)TessDomain);
  MDVals[kDxilHSStateTessellatorPartitioning   ] = Uint32ToConstMD((unsigned)TessPartitioning);
  MDVals[kDxilHSStateTessellatorOutputPrimitive] = Uint32ToConstMD((unsigned)TessOutputPrimitive);
  MDVals[kDxilHSStateMaxTessellationFactor     ] = FloatToConstMD(MaxTessFactor);

  return MDNode::get(m_Ctx, MDVals);
}

void DxilMDHelper::LoadDxilHSState(const MDOperand &MDO,
                                   Function *&pPatchConstantFunction,
                                   unsigned &InputControlPointCount,
                                   unsigned &OutputControlPointCount,
                                   DXIL::TessellatorDomain &TessDomain,
                                   DXIL::TessellatorPartitioning &TessPartitioning,
                                   DXIL::TessellatorOutputPrimitive &TessOutputPrimitive,
                                   float &MaxTessFactor) {
  IFTBOOL(MDO.get() != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  const MDTuple *pTupleMD = dyn_cast<MDTuple>(MDO.get());
  IFTBOOL(pTupleMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  IFTBOOL(pTupleMD->getNumOperands() == kDxilHSStateNumFields, DXC_E_INCORRECT_DXIL_METADATA);

  pPatchConstantFunction  = dyn_cast<Function>(ValueMDToValue(pTupleMD->getOperand(kDxilHSStatePatchConstantFunction)));
  InputControlPointCount  = ConstMDToUint32(pTupleMD->getOperand(kDxilHSStateInputControlPointCount));
  OutputControlPointCount = ConstMDToUint32(pTupleMD->getOperand(kDxilHSStateOutputControlPointCount));
  TessDomain              = (DXIL::TessellatorDomain)ConstMDToUint32(pTupleMD->getOperand(kDxilHSStateTessellatorDomain));
  TessPartitioning        = (DXIL::TessellatorPartitioning)ConstMDToUint32(pTupleMD->getOperand(kDxilHSStateTessellatorPartitioning));
  TessOutputPrimitive     = (DXIL::TessellatorOutputPrimitive)ConstMDToUint32(pTupleMD->getOperand(kDxilHSStateTessellatorOutputPrimitive));
  MaxTessFactor           = ConstMDToFloat(pTupleMD->getOperand(kDxilHSStateMaxTessellationFactor));
}

//
// DxilExtraPropertyHelper methods.
//
DxilMDHelper::ExtraPropertyHelper::ExtraPropertyHelper(Module *pModule)
: m_pModule(pModule)
, m_Ctx(pModule->getContext()) {
}

DxilExtraPropertyHelper::DxilExtraPropertyHelper(Module *pModule)
: ExtraPropertyHelper(pModule) {
}

void DxilExtraPropertyHelper::EmitSRVProperties(const DxilResource &SRV, std::vector<Metadata *> &MDVals) {
  // Element type for typed resource.
  if (!SRV.IsStructuredBuffer() && !SRV.IsRawBuffer()) {
    MDVals.emplace_back(DxilMDHelper::Uint32ToConstMD(DxilMDHelper::kDxilTypedBufferElementTypeTag, m_Ctx));
    MDVals.emplace_back(DxilMDHelper::Uint32ToConstMD((unsigned)SRV.GetCompType().GetKind(), m_Ctx));
  }
  // Element stride for structured buffer.
  if (SRV.IsStructuredBuffer()) {
    MDVals.emplace_back(DxilMDHelper::Uint32ToConstMD(DxilMDHelper::kDxilStructuredBufferElementStrideTag, m_Ctx));
    MDVals.emplace_back(DxilMDHelper::Uint32ToConstMD(SRV.GetElementStride(), m_Ctx));
  }
}

void DxilExtraPropertyHelper::LoadSRVProperties(const MDOperand &MDO, DxilResource &SRV) {
  SRV.SetElementStride(SRV.IsRawBuffer() ? 1 : 4);
  SRV.SetCompType(CompType());

  if (MDO.get() == nullptr) {
    return;
  }

  const MDTuple *pTupleMD = dyn_cast<MDTuple>(MDO.get());
  IFTBOOL(pTupleMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  IFTBOOL((pTupleMD->getNumOperands() & 0x1) == 0, DXC_E_INCORRECT_DXIL_METADATA);

  for (unsigned i = 0; i < pTupleMD->getNumOperands(); i += 2) {
    unsigned Tag = DxilMDHelper::ConstMDToUint32(pTupleMD->getOperand(i));
    const MDOperand &MDO = pTupleMD->getOperand(i + 1);

    switch (Tag) {
    case DxilMDHelper::kDxilTypedBufferElementTypeTag:
      DXASSERT_NOMSG(!SRV.IsStructuredBuffer() && !SRV.IsRawBuffer());
      SRV.SetCompType(CompType(DxilMDHelper::ConstMDToUint32(MDO)));
      break;
    case DxilMDHelper::kDxilStructuredBufferElementStrideTag:
      DXASSERT_NOMSG(SRV.IsStructuredBuffer());
      SRV.SetElementStride(DxilMDHelper::ConstMDToUint32(MDO));
      break;
    default:
      DXASSERT(false, "Unknown resource record tag");
    }
  }
}

void DxilExtraPropertyHelper::EmitUAVProperties(const DxilResource &UAV, std::vector<Metadata *> &MDVals) {
  // Element type for typed RW resource.
  if (!UAV.IsStructuredBuffer() && !UAV.IsRawBuffer()) {
    MDVals.emplace_back(DxilMDHelper::Uint32ToConstMD(DxilMDHelper::kDxilTypedBufferElementTypeTag, m_Ctx));
    MDVals.emplace_back(DxilMDHelper::Uint32ToConstMD((unsigned)UAV.GetCompType().GetKind(), m_Ctx));
  }
  // Element stride for structured RW buffer.
  if (UAV.IsStructuredBuffer()) {
    MDVals.emplace_back(DxilMDHelper::Uint32ToConstMD(DxilMDHelper::kDxilStructuredBufferElementStrideTag, m_Ctx));
    MDVals.emplace_back(DxilMDHelper::Uint32ToConstMD(UAV.GetElementStride(), m_Ctx));
  }
}

void DxilExtraPropertyHelper::LoadUAVProperties(const MDOperand &MDO, DxilResource &UAV) {
  UAV.SetElementStride(UAV.IsRawBuffer() ? 1 : 4);
  UAV.SetCompType(CompType());

  if (MDO.get() == nullptr) {
    return;
  }

  const MDTuple *pTupleMD = dyn_cast<MDTuple>(MDO.get());
  IFTBOOL(pTupleMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  IFTBOOL((pTupleMD->getNumOperands() & 0x1) == 0, DXC_E_INCORRECT_DXIL_METADATA);

  for (unsigned i = 0; i < pTupleMD->getNumOperands(); i += 2) {
    unsigned Tag = DxilMDHelper::ConstMDToUint32(pTupleMD->getOperand(i));
    const MDOperand &MDO = pTupleMD->getOperand(i + 1);

    switch (Tag) {
    case DxilMDHelper::kDxilTypedBufferElementTypeTag:
      DXASSERT_NOMSG(!UAV.IsStructuredBuffer() && !UAV.IsRawBuffer());
      UAV.SetCompType(CompType(DxilMDHelper::ConstMDToUint32(MDO)));
      break;
    case DxilMDHelper::kDxilStructuredBufferElementStrideTag:
      DXASSERT_NOMSG(UAV.IsStructuredBuffer());
      UAV.SetElementStride(DxilMDHelper::ConstMDToUint32(MDO));
      break;
    default:
      DXASSERT(false, "Unknown resource record tag");
    }
  }
}

void DxilExtraPropertyHelper::EmitCBufferProperties(const DxilCBuffer &CB, vector<Metadata *> &MDVals) {
  // Emit property to preserve tbuffer kind
  if (CB.GetKind() == DXIL::ResourceKind::TBuffer) {
    MDVals.emplace_back(DxilMDHelper::Uint32ToConstMD(DxilMDHelper::kHLCBufferIsTBufferTag, m_Ctx));
    MDVals.emplace_back(DxilMDHelper::BoolToConstMD(true, m_Ctx));
  }
}

void DxilExtraPropertyHelper::LoadCBufferProperties(const MDOperand &MDO, DxilCBuffer &CB) {
  if (MDO.get() == nullptr)
    return;

  const MDTuple *pTupleMD = dyn_cast<MDTuple>(MDO.get());
  IFTBOOL(pTupleMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  IFTBOOL((pTupleMD->getNumOperands() & 0x1) == 0, DXC_E_INCORRECT_DXIL_METADATA);

  // Override kind for tbuffer that has not yet been converted to SRV.
  CB.SetKind(DXIL::ResourceKind::CBuffer);
  for (unsigned i = 0; i < pTupleMD->getNumOperands(); i += 2) {
    unsigned Tag = DxilMDHelper::ConstMDToUint32(pTupleMD->getOperand(i));
    const MDOperand &MDO = pTupleMD->getOperand(i + 1);

    switch (Tag) {
    case DxilMDHelper::kHLCBufferIsTBufferTag:
      if (DxilMDHelper::ConstMDToBool(MDO)) {
        CB.SetKind(DXIL::ResourceKind::TBuffer);
      }
      break;
    default:
      DXASSERT(false, "Unknown cbuffer tag");
    }
  }
}

void DxilExtraPropertyHelper::EmitSamplerProperties(const DxilSampler &S, std::vector<Metadata *> &MDVals) {
  // Nothing yet.
}

void DxilExtraPropertyHelper::LoadSamplerProperties(const MDOperand &MDO, DxilSampler &S) {
  // Nothing yet.
}

void DxilExtraPropertyHelper::EmitSignatureElementProperties(const DxilSignatureElement &SE, 
                                                             vector<Metadata *> &MDVals) {
  // Output stream, if non-zero.
  if (SE.GetOutputStream() != 0) {
    MDVals.emplace_back(DxilMDHelper::Uint32ToConstMD(DxilMDHelper::kDxilSignatureElementOutputStreamTag, m_Ctx));
    MDVals.emplace_back(DxilMDHelper::Uint32ToConstMD(SE.GetOutputStream(), m_Ctx));
  }

  // Mask of Dynamically indexed components.
  if (SE.GetDynIdxCompMask() != 0) {
    MDVals.emplace_back(DxilMDHelper::Uint32ToConstMD(DxilMDHelper::kDxilSignatureElementDynIdxCompMaskTag, m_Ctx));
    MDVals.emplace_back(DxilMDHelper::Uint32ToConstMD(SE.GetDynIdxCompMask(), m_Ctx));
  }
}

void DxilExtraPropertyHelper::LoadSignatureElementProperties(const MDOperand &MDO, DxilSignatureElement &SE) {
  if (MDO.get() == nullptr)
    return;

  const MDTuple *pTupleMD = dyn_cast<MDTuple>(MDO.get());
  IFTBOOL(pTupleMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  IFTBOOL((pTupleMD->getNumOperands() & 0x1) == 0, DXC_E_INCORRECT_DXIL_METADATA);

  // Stream.
  for (unsigned i = 0; i < pTupleMD->getNumOperands(); i += 2) {
    unsigned Tag = DxilMDHelper::ConstMDToUint32(pTupleMD->getOperand(i));
    const MDOperand &MDO = pTupleMD->getOperand(i + 1);

    switch (Tag) {
    case DxilMDHelper::kDxilSignatureElementOutputStreamTag:
      SE.SetOutputStream(DxilMDHelper::ConstMDToUint32(MDO));
      break;
    case DxilMDHelper::kHLSignatureElementGlobalSymbolTag:
      break;
    case DxilMDHelper::kDxilSignatureElementDynIdxCompMaskTag:
      SE.SetDynIdxCompMask(DxilMDHelper::ConstMDToUint32(MDO));
      break;
    default:
      DXASSERT(false, "Unknown signature element tag");
    }
  }
}

//
// Utilities.
//
bool DxilMDHelper::IsKnownNamedMetaData(llvm::NamedMDNode &Node) {
  StringRef name = Node.getName();
  for (unsigned i = 0; i < DxilMDNames.size(); i++) {
    if (name == DxilMDNames[i]) {
      return true;
    }
  }
  return false;
}

ConstantAsMetadata *DxilMDHelper::Int32ToConstMD(int32_t v, LLVMContext &Ctx) {
  return ConstantAsMetadata::get(Constant::getIntegerValue(IntegerType::get(Ctx, 32), APInt(32, v)));
}

ConstantAsMetadata *DxilMDHelper::Int32ToConstMD(int32_t v) {
  return DxilMDHelper::Int32ToConstMD(v, m_Ctx);
}

ConstantAsMetadata *DxilMDHelper::Uint32ToConstMD(unsigned v, LLVMContext &Ctx) {
  return ConstantAsMetadata::get(Constant::getIntegerValue(IntegerType::get(Ctx, 32), APInt(32, v)));
}

ConstantAsMetadata *DxilMDHelper::Uint32ToConstMD(unsigned v) {
  return DxilMDHelper::Uint32ToConstMD(v, m_Ctx);
}

ConstantAsMetadata *DxilMDHelper::Uint64ToConstMD(uint64_t v, LLVMContext &Ctx) {
  return ConstantAsMetadata::get(Constant::getIntegerValue(IntegerType::get(Ctx, 64), APInt(64, v)));
}
ConstantAsMetadata *DxilMDHelper::Uint64ToConstMD(uint64_t v) {
  return DxilMDHelper::Uint64ToConstMD(v, m_Ctx);
}
ConstantAsMetadata *DxilMDHelper::Int8ToConstMD(int8_t v) {
  return ConstantAsMetadata::get(Constant::getIntegerValue(IntegerType::get(m_Ctx, 8), APInt(8, v)));
}
ConstantAsMetadata *DxilMDHelper::Uint8ToConstMD(uint8_t v) {
  return ConstantAsMetadata::get(Constant::getIntegerValue(IntegerType::get(m_Ctx, 8), APInt(8, v)));
}

ConstantAsMetadata *DxilMDHelper::BoolToConstMD(bool v, LLVMContext &Ctx) {
  return ConstantAsMetadata::get(Constant::getIntegerValue(IntegerType::get(Ctx, 1), APInt(1, v ? 1 : 0)));
}
ConstantAsMetadata *DxilMDHelper::BoolToConstMD(bool v) {
  return DxilMDHelper::BoolToConstMD(v, m_Ctx);
}

ConstantAsMetadata *DxilMDHelper::FloatToConstMD(float v) {
  return ConstantAsMetadata::get(ConstantFP::get(m_Ctx, APFloat(v)));
}

int32_t DxilMDHelper::ConstMDToInt32(const MDOperand &MDO) {
  ConstantInt *pConst = mdconst::extract<ConstantInt>(MDO);
  IFTBOOL(pConst != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  return (int32_t)pConst->getZExtValue();
}

unsigned DxilMDHelper::ConstMDToUint32(const MDOperand &MDO) {
  ConstantInt *pConst = mdconst::extract<ConstantInt>(MDO);
  IFTBOOL(pConst != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  return (unsigned)pConst->getZExtValue();
}

uint64_t DxilMDHelper::ConstMDToUint64(const MDOperand &MDO) {
  ConstantInt *pConst = mdconst::extract<ConstantInt>(MDO);
  IFTBOOL(pConst != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  return pConst->getZExtValue();
}

int8_t DxilMDHelper::ConstMDToInt8(const MDOperand &MDO) {
  ConstantInt *pConst = mdconst::extract<ConstantInt>(MDO);
  IFTBOOL(pConst != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  return (int8_t)pConst->getZExtValue();
}

uint8_t DxilMDHelper::ConstMDToUint8(const MDOperand &MDO) {
  ConstantInt *pConst = mdconst::extract<ConstantInt>(MDO);
  IFTBOOL(pConst != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  return (uint8_t)pConst->getZExtValue();
}

bool DxilMDHelper::ConstMDToBool(const MDOperand &MDO) {
  ConstantInt *pConst = mdconst::extract<ConstantInt>(MDO);
  IFTBOOL(pConst != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  return pConst->getZExtValue() != 0;
}

float DxilMDHelper::ConstMDToFloat(const MDOperand &MDO) {
  ConstantFP *pConst = mdconst::extract<ConstantFP>(MDO);
  IFTBOOL(pConst != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  return pConst->getValueAPF().convertToFloat();
}

string DxilMDHelper::StringMDToString(const MDOperand &MDO) {
  MDString *pMDString = dyn_cast<MDString>(MDO.get());
  IFTBOOL(pMDString != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  return pMDString->getString();
}

Value *DxilMDHelper::ValueMDToValue(const MDOperand &MDO) {
  IFTBOOL(MDO.get() != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  ValueAsMetadata *pValAsMD = dyn_cast<ValueAsMetadata>(MDO.get());
  IFTBOOL(pValAsMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  Value *pValue = pValAsMD->getValue();
  IFTBOOL(pValue != nullptr, DXC_E_INCORRECT_DXIL_METADATA);
  return pValue;
}

MDTuple *DxilMDHelper::Uint32VectorToConstMDTuple(const std::vector<unsigned> &Vec) {
  vector<Metadata *> MDVals;

  MDVals.resize(Vec.size());
  for (size_t i = 0; i < Vec.size(); i++) {
    MDVals[i] = Uint32ToConstMD(Vec[i]);
  }
  return MDNode::get(m_Ctx, MDVals);
}

void DxilMDHelper::ConstMDTupleToUint32Vector(MDTuple *pTupleMD, std::vector<unsigned> &Vec) {
  IFTBOOL(pTupleMD != nullptr, DXC_E_INCORRECT_DXIL_METADATA);

  Vec.resize(pTupleMD->getNumOperands());
  for (size_t i = 0; i < pTupleMD->getNumOperands(); i++) {
    Vec[i] = ConstMDToUint32(pTupleMD->getOperand(i));
  }
}

bool DxilMDHelper::IsMarkedPrecise(const Instruction *inst) {
  int32_t val = 0;
  if (MDNode *precise = inst->getMetadata(kDxilPreciseAttributeMDName)) {
    assert(precise->getNumOperands() == 1);
    val = ConstMDToInt32(precise->getOperand(0));
  }
  return val;
}

void DxilMDHelper::MarkPrecise(Instruction *I) {
  LLVMContext &Ctx = I->getContext();
  MDNode *preciseNode = MDNode::get(
    Ctx,
    { ConstantAsMetadata::get(ConstantInt::get(Type::getInt32Ty(Ctx), 1)) });

  I->setMetadata(DxilMDHelper::kDxilPreciseAttributeMDName, preciseNode);
}

} // namespace hlsl
