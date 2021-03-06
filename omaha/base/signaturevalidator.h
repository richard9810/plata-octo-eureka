// Copyright 2002-2010 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// ========================================================================

#ifndef OMAHA_BASE_SIGNATUREVALIDATOR_H_
#define OMAHA_BASE_SIGNATUREVALIDATOR_H_

#include <windows.h>
#include <wincrypt.h>
#include <atlstr.h>
#pragma warning(push)
// C4548: expression before comma has no effect
#pragma warning(disable : 4548)
#include <vector>
#pragma warning(pop)

// VerifyAuthenticodeSignature() and VerifySigneeIsGoogle() should
// always be used together.
//
// VerifyAuthenticodeSignature() verifies that the signature is valid and has
// a trusted chain. It also verifies that the signing certificate was valid
// at the time it was used to sign. If all are true, it returns S_OK.
// Even if the certificate has expired since it was used to sign, the signature
// is valid and VerifyAuthenticodeSignature() returns S_OK.
//
// If allow_network_check is true, VerifyAuthenticodeSignature() will
// also check the Certificate Revocation List (CRL). If the certificate was
// revoked after it was used to sign, it will return S_OK. Otherwise, it fails.
// At no time does VerifyAuthenticodeSignature() check whether the certificate
// is currently valid.
//
// VerifySigneeIsGoogle() verifies that Google signed the file. It does not
// check the certificate chain, CRL, or anything related to the timestamp.
//
// Some of the helper classes and methods allow the caller to check whether the
// certificate is valid now. The above methods do not check this.

namespace omaha {

// Class: CertInfo
//
// CertInfo holds all sensible details of a certificate. During verification of
// a signature, one CertInfo object is made for each certificate encountered in
// the signature.
class CertInfo {
 public:
  // certificate issuing company name e.g. "Google Inc".
  CString issuing_company_name_;

  // a company may own multiple certificates.
  // so this tells which dept owns this certificate.
  CString issuing_dept_name_;

  // trust-authority (or trust-provider) name. e.g. "Verisign, Inc.".
  CString trust_authority_name_;

  // SHA-1 hash of the certificate's raw bytes. This is the value that is
  // displayed by the operating system in the certificate's details.
  CString thumbprint_;

  // SHA-256 hash of the certificate subject's public key.
  CString public_key_hash_;

  // validity period start-date
  FILETIME not_valid_before_;

  // validity period end-date
  FILETIME not_valid_after_;

  // CERT_CONTEXT structure, defined by Crypto API, contains all the info about
  // the certificate.
  const CERT_CONTEXT *cert_context_;

  explicit CertInfo(const CERT_CONTEXT* given_cert_context);

  ~CertInfo();

  // IsValidNow() functions returns true if this certificate is valid at this
  // moment, based on the validity period specified in the certificate.
  bool IsValidNow() const;

  // AsString() is a utility function that's used for printing CertInfo details.
  CString AsString() const {
    CString cert_info_str =
        _T("Issuing Company: \"") + issuing_company_name_ +
        _T("\"  Dept: \"") + issuing_dept_name_ +
        _T("\"  Trust Provider: \"") + trust_authority_name_ +
        _T("\"  Valid From: \"") + this->FileTimeToString(&not_valid_before_) +
        _T("\"  Valid To: \"") + this->FileTimeToString(&not_valid_after_) +
        _T("\"");
    return cert_info_str;
  }


  // FileTimeToString() is just a convenience function to print FILETIME.
  static CString FileTimeToString(const FILETIME* ft);

  // Given a cerificate context, this function extracts the subject/signee
  // company name and its dept name(orgnanizational-unit-name, as they call it).
  // Optionally, you could also retrieve trust-authority name.
  static bool ExtractIssuerInfo(const CERT_CONTEXT* cert_context,
                                CString* orgn_name,
                                CString* orgn_dept_name,
                                CString* trust_authority = NULL);

  static bool ExtractThumbprint(const CERT_CONTEXT* cert_context,
                                CString* thumbprint);

  static bool ExtractPublicKeyHash(const CERT_CONTEXT* cert_context,
                                   CString* public_key_hash);

 private:
  // Extracts the specified field from the certificate. Only the first value for
  // the field is extracted if multiple values are present. Returns true if
  // the field is extracted successfully. Returns false if an error occurred
  // during the extraction or the field was not found.
  static bool ExtractField(const CERT_CONTEXT* cert_context,
                           const char* field_name,
                           CString* field_value);
};

// CertList is a container for a list of certificates. It is used to hold all
// the certificates found in the signature of a signed file. In addition, it
// also provides interface to fetch certificates matching to a particular
// criterion.
//
// Internally, CertList contains basically a vector of CertInfo* pointers.
// The only reason why CertList is created as opposed to simply putting all
// the certificates in a vector<CertInfo*> is to avoid memory-leaks. CertList
// contains a list of CertInfo pointers and users don't have to worry about
// freeing those pointers. On the other hand, if you use vector<CertInfo>
// instead, it results in unwanted copying of CertInfo objects around.
class CertList {
 public:
  // Constructor
  CertList() {}

  // Destructor
  ~CertList() {
    for (unsigned int inx = 0; inx < cert_list_.size(); ++inx)
      delete cert_list_[inx];
    cert_list_.clear();
  }

  // size() returns the number of certificates in this CertList
  size_t size() const {
    return cert_list_.size();
  }

  // AddCertificate() is used to add a certificate to CertList.
  // NOTE that once a certificate is added, CertList takes ownership of that
  // CertInfo object.
  void AddCertificate(CertInfo* cert) {
    cert_list_.push_back(cert);
  }

  // FindFirstCert() finds the first certificate that exactly matches the given
  // criteria.
  void FindFirstCert(const CertInfo** result_cert_info,
                     const std::vector<CString>& company_name_to_match,
                     const CString &orgn_unit_to_match,
                     const CString &trust_authority_to_match,
                     bool check_cert_is_valid_now) const;

  typedef std::vector<CertInfo*> CertInfoList;

 private:
  CertInfoList cert_list_;
};

// ExtractAllCertificatesFromSignature() takes in a signed file, extracts all
// the certificates related to its signature and returns them in a CertList
// object. `subject_name` can be used to narrow the list of certificates to only
// those that match the given subject string.
void ExtractAllCertificatesFromSignature(const wchar_t* signed_file,
                                         const wchar_t* subject_name,
                                         CertList* cert_list);

// Returns true if the subject of the certificate exactly matches the first CN
// name.
//
// The function enforces an additional check against the public key of the
// certificate. Pinning to specific public keys mitigates the risk of accepting
// certificates issued by weak CAs. The list of expected hashes to pin the
// certificate to is provided by the optional 'expected_hashes' parameter.
//
// The function can verify that the certificate is valid at the time of the
// call.
HRESULT VerifyCertificate(const wchar_t* signed_file,
                          const std::vector<CString>& subject,
                          bool check_cert_is_valid_now,
                          const std::vector<CString>* expected_hashes);

// Returns S_OK if a given signed file contains a signature
// that could be successfully verified using one of the trust providers
// IE relies on. This means that, whoever signed the file, they should've signed
// using certificate issued by a well-known (to IE) trust provider like
// Verisign, Inc.
HRESULT VerifyAuthenticodeSignature(const wchar_t* signed_file,
                                    bool allow_network_check);

// Returns true if a given signed file contains a valid signature.
inline bool SignatureIsValid(const wchar_t* signed_file,
                             bool allow_network_check) {
  return VerifyAuthenticodeSignature(signed_file, allow_network_check) == S_OK;
}

// Gets the timestamp for the file's signature.
HRESULT GetSigningTime(const wchar_t* signed_file, SYSTEMTIME* signing_time);

// Verifies that the file was signed within the specified number of days.
HRESULT VerifyFileSignedWithinDays(const wchar_t* signed_file, int days);

}  // namespace omaha

#endif  // OMAHA_BASE_SIGNATUREVALIDATOR_H_
