#pragma once
#include "afx.h"
#include "afxdlgs.h"
#include <cstdint>
#include <vector>

enum class EBhLayout : int { XYZ = 0, XZY, YXZ, YZX, ZXY, ZYX };

struct CBhANAVolume	 //anatomical data volume, for example, T1-weighted MRI data
{
public:
	int64_t m_nW = 0;
	int64_t m_nH = 0;
	int64_t m_nD = 0;
	double m_dSpaceX = 1.0;
	double m_dSpaceY = 1.0;
	double m_dSpaceZ = 1.0;
	double m_dOriginMm[3] = { 0.0, 0.0, 0.0 };
	EBhLayout m_uLayout = EBhLayout::XYZ;
	uint16_t m_uValueMask = 0x0FFF;

	std::vector<uint16_t> m_vRaw;
	CString m_sName;
	CString m_sImportMsg;
	CString m_sPathName;

	CString m_sMagicHead=_T("MRICT3DDATA");
	DWORD m_dwVersion = 20261030;

	int serialize(CArchive& ar)
	{
		//read and write magic head and version
		if (ar.IsStoring()) {
			ar << m_sMagicHead << m_dwVersion;
		}
		else {
			CString sMagicHead;
			ar >> sMagicHead >> m_dwVersion;
			if (0 != sMagicHead.Compare(m_sMagicHead)  ) {
				AfxMessageBox(_T("Invalid file format!"));
				return 0;
			}
		}

		//read and write the  real data
		if (ar.IsStoring()) {
 			ar << m_nW << m_nH << m_nD;
			ar << m_dSpaceX << m_dSpaceY << m_dSpaceZ;
			ar.Write(m_dOriginMm, sizeof(m_dOriginMm));
			int nLayout = static_cast<int>(m_uLayout);
			ar << nLayout;
			ar << m_uValueMask;
			int64_t nCount = static_cast<int64_t>(m_vRaw.size());
			ar << nCount;
			if (nCount > 0) {
				ar.Write(m_vRaw.data(), nCount * sizeof(uint16_t));
			}
			ar << m_sImportMsg;
		}
		else {
 			ar >> m_nW >> m_nH >> m_nD;
			ar >> m_dSpaceX >> m_dSpaceY >> m_dSpaceZ;
			ar.Read(m_dOriginMm, sizeof(m_dOriginMm));
			int nLayout = 0;
			ar >> nLayout;
			m_uLayout = static_cast<EBhLayout>(nLayout);
			ar >> m_uValueMask;
			int64_t nCount = 0;
			ar >> nCount;
			m_vRaw.resize(static_cast<size_t>(nCount));
			if (nCount > 0) {
				ar.Read(m_vRaw.data(), nCount * sizeof(uint16_t));
			}

			ar >> m_sImportMsg;
		}
		return 1;
	}

	int openFilePath(const CString& sPathName) {
		CFile file(sPathName, CFile::modeRead | CFile::shareDenyWrite);
		CArchive ar(&file, CArchive::load);
		serialize(ar);
		ar.Close();
		file.Close();
		return 1;
	}
	int openStd() {

		CFileDialog dlg(TRUE, _T("amc"), NULL, OFN_HIDEREADONLY | OFN_FILEMUSTEXIST, _T("Anatomical MRI/CT Files (*.amc)|*.amc|All Files (*.*)|*.*||"));
		dlg.m_ofn.lpstrTitle = _T("Open anatomical MRI/CT File");
		if(dlg.DoModal() == IDOK) {
			m_sPathName = dlg.GetPathName();
			CFile file(m_sPathName, CFile::modeRead | CFile::shareDenyWrite);
			CArchive ar(&file, CArchive::load);
			serialize(ar);
			ar.Close();
			file.Close();
			return 1;
		}
		return 0;
	}

	int saveStd() {
		CFileDialog dlg(FALSE, _T("amc"), NULL, OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT, _T("Anatomical MRI/CT Files (*.amc)|*.amc|All Files (*.*)|*.*||"));
		if(dlg.DoModal() == IDOK) {
			//ensure the file has the correct extension
			CString strPathName = dlg.GetPathName();
			if(strPathName.Right(4).CompareNoCase(_T(".amc")) != 0) {
				strPathName += _T(".amc");
			}
			CFile file(strPathName, CFile::modeCreate | CFile::modeWrite | CFile::shareDenyWrite);
			CArchive ar(&file, CArchive::store);
			serialize(ar);
			ar.Close();
			file.Close();
			return 1;
		}
		return 0;
	}
};

struct CBhFunVolume {	//functional data volume, for example, fMRI data
	int64_t m_nTime = 0;
	int64_t m_nW = 0;
	int64_t m_nH = 0;
	int64_t m_nD = 0;
	int64_t m_nItems = 0;
	int64_t m_nTimeCurrent = 0;
	int64_t m_nItemActive = 0;
	double m_dStepX = 1.0;
	double m_dStepY = 1.0;
	double m_dStepZ = 1.0;
	double m_dOriginMm[3] = { 0.0, 0.0, 0.0 };
 	std::vector<double> m_vData;
	CString m_sName;
	CString m_sImportMsg;
	CString m_sPathName;

	CString m_sMagicHead = _T("MEGEEGSOURCEDATA");
	DWORD m_dwVersion = 20261030;

	int serialize(CArchive& ar)
	{
		//read and write magic head and version
		if (ar.IsStoring()) {
			ar << m_sMagicHead << m_dwVersion;
		}
		else {
			CString sMagicHead;
			ar >> sMagicHead >> m_dwVersion;
			if (0 != sMagicHead.Compare(m_sMagicHead)) {
				AfxMessageBox(_T("Invalid file format!"));
				return 0;
			}
		}

		//read and write the  real data
		if (ar.IsStoring()) {
			ar << m_nTime << m_nW << m_nH << m_nD;
			ar << m_nItems << m_nTimeCurrent;
			ar << m_nItemActive;
			ar << m_dStepX << m_dStepY << m_dStepZ;
			ar.Write(m_dOriginMm, sizeof(m_dOriginMm));
			int64_t nCount = static_cast<int64_t>(m_vData.size());
			ar << nCount;
			if (nCount > 0) {
				ar.Write(m_vData.data(), nCount * sizeof(double));
			}
			ar << m_sName;
			ar << m_sImportMsg;
 		}
		else {
			ar >> m_nTime >> m_nW >> m_nH >> m_nD;
			ar >> m_nItems >> m_nTimeCurrent;
			ar >> m_nItemActive;
			ar >> m_dStepX >> m_dStepY >> m_dStepZ;
			ar.Read(m_dOriginMm, sizeof(m_dOriginMm));
			int64_t nCount = 0;
			ar >> nCount;
			m_vData.resize(static_cast<size_t>(nCount));
			if (nCount > 0) {
				ar.Read(m_vData.data(), nCount * sizeof(double));
			}
			ar >> m_sName;
			ar >> m_sImportMsg;
		}
		return 1;
	}

	int openFilePath(const CString& sPathName) {
		CFile file(sPathName, CFile::modeRead | CFile::shareDenyWrite);
		CArchive ar(&file, CArchive::load);
		serialize(ar);
		ar.Close();
		file.Close();
		return 1;
	}

	int openStd() {

		CFileDialog dlg(TRUE, _T("mes"), NULL, OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT, _T("Source Files (*.mes)|*.mes|All Files (*.*)|*.*||"));
		dlg.m_ofn.lpstrTitle = _T("Open functional source File from MEG/EEG/iEEG/sEEG  ");

		if (dlg.DoModal() == IDOK) {
			m_sPathName = dlg.GetPathName();

			CFile file(dlg.GetPathName(), CFile::modeRead | CFile::shareDenyWrite);
			CArchive ar(&file, CArchive::load);
			serialize(ar);
			ar.Close();
			file.Close();
			return 1;
		}
		return 0;
	}

	int saveStd() {
		CFileDialog dlg(FALSE, _T("mes"), NULL, OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT, _T("Source Files (*.mes)|*.mes|All Files (*.*)|*.*||"));
		if (dlg.DoModal() == IDOK) {
			//ensure the file has the correct extension
			CString strPathName = dlg.GetPathName();
			if (strPathName.Right(4).CompareNoCase(_T(".mes")) != 0) {
				strPathName += _T(".mes");
			}
			CFile file(strPathName, CFile::modeCreate | CFile::modeWrite | CFile::shareDenyWrite);
			CArchive ar(&file, CArchive::store);
			serialize(ar);
			ar.Close();
			file.Close();
			return 1;
		}
		return 0;
	}


};

