#ifndef _QCC_KEYINFO_ECC_H
#define _QCC_KEYINFO_ECC_H
/**
 * @file
 *
 * This file provide ECC public key info
 */

/******************************************************************************
 * Copyright AllSeen Alliance. All rights reserved.
 *
 *    Permission to use, copy, modify, and/or distribute this software for any
 *    purpose with or without fee is hereby granted, provided that the above
 *    copyright notice and this permission notice appear in all copies.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 ******************************************************************************/

#include <qcc/platform.h>
#include <qcc/KeyInfo.h>
#include <qcc/CryptoECC.h>

namespace qcc {

class SigInfo {

  public:

    static const size_t ALGORITHM_ECDSA_SHA_256 = 0;

    /**
     * Default constructor.
     */
    SigInfo(KeyInfo::FormatType format) : format(format), algorithm(0xFF)
    {
    }

    /**
     * desstructor.
     */
    virtual ~SigInfo()
    {
    }

    /**
     * Get the format
     * @return the format
     */
    const KeyInfo::FormatType GetFormat() const
    {
        return format;
    }

    /**
     * Retrieve the signature algorithm
     * @return the signature ECC algorithm
     */
    const uint8_t GetAlgorithm() const
    {
        return algorithm;
    }

    /**
     * Virtual initializer to be implemented by derived classes.  The derired
     * class should call the protected SigInfo::SetAlgorithm() method to set
     * the signature algorithm.
     */

    virtual void Init() = 0;

  protected:

    /**
     * Set the signature algorithm
     */
    void SetAlgorithm(uint8_t algorithm)
    {
        this->algorithm = algorithm;
    }


  private:
    /**
     * Assignment operator is private
     */
    SigInfo& operator=(const SigInfo& other);

    /**
     * Copy constructor is private
     */
    SigInfo(const SigInfo& other);

    KeyInfo::FormatType format;
    uint8_t algorithm;
};

class SigInfoECC : public SigInfo {

  public:

    /**
     * Default constructor.
     */
    SigInfoECC() : SigInfo(KeyInfo::FORMAT_ALLJOYN)
    {
        Init();
    }

    virtual void Init() {
        SetAlgorithm(ALGORITHM_ECDSA_SHA_256);
        memset(&sig, 0, sizeof(ECCSignature));
    }

    /**
     * desstructor.
     */
    virtual ~SigInfoECC()
    {
    }

    /**
     * Assign the R coordinate
     * @param rCoord the R coordinate value to copy
     */
    void SetRCoord(const uint8_t* rCoord)
    {
        memcpy(sig.r, rCoord, ECC_COORDINATE_SZ);
    }
    /**
     * Retrieve the R coordinate value
     * @return the R coordinate value.  It's a pointer to an internal buffer. Its lifetime is the same as the object's lifetime.
     */
    const uint8_t* GetRCoord() const
    {
        return sig.r;
    }

    /**
     * Assign the S coordinate
     * @param sCoord the S coordinate value to copy
     */
    void SetSCoord(const uint8_t* sCoord)
    {
        memcpy(sig.s, sCoord, ECC_COORDINATE_SZ);
    }

    /**
     * Retrieve the S coordinate value
     * @return the S coordinate value.  It's a pointer to an internal buffer. Its lifetime is the same as the object's lifetime.
     */
    const uint8_t* GetSCoord() const
    {
        return sig.s;
    }

    /**
     * Set the signature.  The signature is copied into the internal buffer.
     */
    void SetSignature(const ECCSignature* sig)
    {
        memcpy(&this->sig, sig, sizeof(ECCSignature));
    }
    /**
     * Get the signature.
     * @return the signature.
     */
    const ECCSignature* GetSignature() const
    {
        return &sig;
    }

  private:
    /**
     * Assignment operator is private
     */
    SigInfoECC& operator=(const SigInfoECC& other);

    /**
     * Copy constructor is private
     */
    SigInfoECC(const SigInfoECC& other);

    ECCSignature sig;
};

/**
 * ECC KeyInfo
 */
class KeyInfoECC : public KeyInfo {

  public:

    /**
     * The ECC key type
     */
    static const size_t KEY_TYPE = 0;

    /**
     * The ECC algorithm
     */

    /**
     * Default constructor.
     */
    KeyInfoECC() : KeyInfo(FORMAT_ALLJOYN), curve(Crypto_ECC::ECC_NIST_P256)
    {
    }

    /**
     * constructor.
     */
    KeyInfoECC(uint8_t curve) : KeyInfo(FORMAT_ALLJOYN), curve(curve)
    {
    }

    /**
     * Default destructor.
     */
    virtual ~KeyInfoECC()
    {
    }

    /**
     * Retrieve the ECC algorithm
     * @return the ECC algorithm
     */
    const uint8_t GetAlgorithm() const
    {
        return SigInfo::ALGORITHM_ECDSA_SHA_256;
    }

    /**
     * Retrieve the ECC curve type.
     * @return the ECC curve type
     */
    const uint8_t GetCurve() const
    {
        return curve;
    }

    virtual const ECCPublicKey* GetPublicKey() const
    {
        return NULL;
    }

    virtual void SetPublicKey(const ECCPublicKey* key)
    {
        QCC_UNUSED(key);
    }

    /**
     * The required size of the exported byte array.
     * @return the required size of the exported byte array
     */

    const size_t GetExportSize();

    /**
     * Export the KeyInfo data to a byte array.
     * @param[in,out] buf the pointer to a byte array.  The caller must allocateenough memory based on call GetExportSize().
     * @return ER_OK for success; otherwise, an error code
     */

    QStatus Export(uint8_t* buf);

    /**
     * Import a byte array generated by a KeyInfo Export.
     * @param buf the export data
     * @param count the size of the export data
     * @return ER_OK for success; otherwise, an error code
     */

    QStatus Import(const uint8_t* buf, size_t count);

    virtual qcc::String ToString() const;

    bool operator==(const KeyInfoECC& ki) const
    {
        if (curve != ki.curve) {
            return false;
        }

        return KeyInfo::operator==(ki);
    }

    /**
     * Assignment operator for KeyInfoECC
     */
    KeyInfoECC& operator=(const KeyInfoECC& other) {
        if (&other != this) {
            KeyInfo::operator=(other);
            curve = other.curve;
        }
        return *this;
    }

    /**
     * Copy constructor for KeyInfoECC
     */
    KeyInfoECC(const KeyInfoECC& other) :
        KeyInfo(other), curve(other.curve) {
    }

  private:

    uint8_t curve;
};

/**
 * NIST P-256 ECC KeyInfo
 */
class KeyInfoNISTP256 : public KeyInfoECC {

  public:

    /**
     * Default constructor.
     */
    KeyInfoNISTP256() : KeyInfoECC(Crypto_ECC::ECC_NIST_P256)
    {
        /* using uncompressed */
        pubkey.form = 0x4;
    }

    /**
     * Copy constructor.
     */
    KeyInfoNISTP256(const KeyInfoNISTP256& other) : KeyInfoECC(Crypto_ECC::ECC_NIST_P256)
    {
        SetKeyId(other.GetKeyId(), other.GetKeyIdLen());
        SetPublicCtx(other.GetPublicCtx());
    }

    /**
     * Default destructor.
     */
    virtual ~KeyInfoNISTP256()
    {
    }

    /**
     * Assign the X coordinate
     * @param xCoord the X coordinate value to copy
     */
    void SetXCoord(const uint8_t* xCoord)
    {
        memcpy(pubkey.key.x, xCoord, ECC_COORDINATE_SZ);
    }

    /**
     * Retrieve the X coordinate value
     * @return the ECC X coordinate value.  It's a pointer to an internal buffer. Its lifetime is the same as the object's lifetime.
     */
    const uint8_t* GetXCoord() const
    {
        return pubkey.key.x;
    }

    /**
     * Assign the Y coordinate
     * @param yCoord the Y coordinate value to copy
     */
    void SetYCoord(const uint8_t* yCoord)
    {
        memcpy(pubkey.key.y, yCoord, ECC_COORDINATE_SZ);
    }

    /**
     * Retrieve the Y coordinate value
     * @return the ECC Y coordinate value.  It's a pointer to an internal buffer. Its lifetime is the same as the object's lifetime.
     */
    const uint8_t* GetYCoord() const
    {
        return pubkey.key.y;
    }

    const uint8_t* GetPublicCtx() const
    {
        return (const uint8_t*) &pubkey;
    }

    const ECCPublicKey* GetPublicKey() const
    {
        return &pubkey.key;
    }

    size_t GetPublicSize() const
    {
        return sizeof (pubkey);
    }

    void SetPublicCtx(const uint8_t* ctx)
    {
        memcpy((uint8_t*) &pubkey, ctx, sizeof (pubkey));
    }

    void SetPublicKey(const ECCPublicKey* key)
    {
        /* using uncompressed */
        pubkey.form = 0x4;
        memcpy((uint8_t*) &pubkey.key, (uint8_t*) key, sizeof (pubkey.key));
    }

    /**
     * The required size of the exported byte array.
     * @return the required size of the exported byte array
     */

    const size_t GetExportSize();

    /**
     * Export the KeyInfo data to a byte array.
     * @param[in,out] buf the pointer to a byte array.  The caller must allocateenough memory based on call GetExportSize().
     * @return ER_OK for success; otherwise, an error code
     */

    QStatus Export(uint8_t* buf);

    /**
     * Import a byte array generated by a KeyInfo Export.
     * @param buf the export data
     * @param count the size of the export data
     * @return ER_OK for success; otherwise, an error code
     */

    QStatus Import(const uint8_t* buf, size_t count);

    virtual qcc::String ToString() const;

    bool operator==(const KeyInfoNISTP256& ki) const
    {
        if (pubkey.form != ki.pubkey.form) {
            return false;
        }

        if (pubkey.key != ki.pubkey.key) {
            return false;
        }

        return KeyInfoECC::operator==(ki);
    }

    /**
     * Assign operator for KeyInfoNISTP256
     *
     * @param[in] other the KeyInfoNISTP256 to assign
     */

    KeyInfoNISTP256& operator=(const KeyInfoNISTP256& other)
    {
        if (this != &other) {
            SetKeyId(other.GetKeyId(), other.GetKeyIdLen());
            SetPublicCtx(other.GetPublicCtx());
        }
        return *this;
    }

  private:

    struct {
        uint8_t form;
        ECCPublicKey key;
    } pubkey;
};

} /* namespace qcc */


#endif
