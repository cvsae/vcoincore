// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PRIMITIVES_TRANSACTION_H
#define BITCOIN_PRIMITIVES_TRANSACTION_H

#include "amount.h"
#include "script/script.h"
#include "serialize.h"
#include "uint256.h"

static const int SERIALIZE_TRANSACTION_NO_WITNESS = 0x40000000;

static const int WITNESS_SCALE_FACTOR = 4;

/** An outpoint - a combination of a transaction hash and an index n into its vout */
class COutPoint
{
public:
    uint256 hash;
    uint32_t n;

    COutPoint() { SetNull(); }
    COutPoint(uint256 hashIn, uint32_t nIn) { hash = hashIn; n = nIn; }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(hash);
        READWRITE(n);
    }

    void SetNull() { hash.SetNull(); n = (uint32_t) -1; }
    bool IsNull() const { return (hash.IsNull() && n == (uint32_t) -1); }

    friend bool operator<(const COutPoint& a, const COutPoint& b)
    {
        int cmp = a.hash.Compare(b.hash);
        return cmp < 0 || (cmp == 0 && a.n < b.n);
    }

    friend bool operator==(const COutPoint& a, const COutPoint& b)
    {
        return (a.hash == b.hash && a.n == b.n);
    }

    friend bool operator!=(const COutPoint& a, const COutPoint& b)
    {
        return !(a == b);
    }

    std::string ToString() const;
};

/** An input of a transaction.  It contains the location of the previous
 * transaction's output that it claims and a signature that matches the
 * output's public key.
 */
class CTxIn
{
public:
    COutPoint prevout;
    CScript scriptSig;
    uint32_t nSequence;

    /* Setting nSequence to this value for every input in a transaction
     * disables nLockTime. */
    static const uint32_t SEQUENCE_FINAL = 0xffffffff;

    /* Below flags apply in the context of BIP 68*/
    /* If this flag set, CTxIn::nSequence is NOT interpreted as a
     * relative lock-time. */
    static const uint32_t SEQUENCE_LOCKTIME_DISABLE_FLAG = (1 << 31);

    /* If CTxIn::nSequence encodes a relative lock-time and this flag
     * is set, the relative lock-time has units of 512 seconds,
     * otherwise it specifies blocks with a granularity of 1. */
    static const uint32_t SEQUENCE_LOCKTIME_TYPE_FLAG = (1 << 22);

    /* If CTxIn::nSequence encodes a relative lock-time, this mask is
     * applied to extract that lock-time from the sequence field. */
    static const uint32_t SEQUENCE_LOCKTIME_MASK = 0x0000ffff;

    /* In order to use the same number of bits to encode roughly the
     * same wall-clock duration, and because blocks are naturally
     * limited to occur every 600s on average, the minimum granularity
     * for time-based relative lock-time is fixed at 512 seconds.
     * Converting from CTxIn::nSequence to seconds is performed by
     * multiplying by 512 = 2^9, or equivalently shifting up by
     * 9 bits. */
    static const int SEQUENCE_LOCKTIME_GRANULARITY = 9;

    CTxIn()
    {
        nSequence = SEQUENCE_FINAL;
    }

    explicit CTxIn(COutPoint prevoutIn, CScript scriptSigIn=CScript(), uint32_t nSequenceIn=SEQUENCE_FINAL);
    CTxIn(uint256 hashPrevTx, uint32_t nOut, CScript scriptSigIn=CScript(), uint32_t nSequenceIn=SEQUENCE_FINAL);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(prevout);
        READWRITE(*(CScriptBase*)(&scriptSig));
        READWRITE(nSequence);
    }

    friend bool operator==(const CTxIn& a, const CTxIn& b)
    {
        return (a.prevout   == b.prevout &&
                a.scriptSig == b.scriptSig &&
                a.nSequence == b.nSequence);
    }

    friend bool operator!=(const CTxIn& a, const CTxIn& b)
    {
        return !(a == b);
    }

    std::string ToString() const;
};

/** An output of a transaction.  It contains the public key that the next input
 * must be able to sign with to claim it.
 */
class CTxOut
{
public:
    CAmount nValue;
    CScript scriptPubKey;

    CTxOut()
    {
        SetNull();
    }

    CTxOut(const CAmount& nValueIn, CScript scriptPubKeyIn);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nValue);
        READWRITE(*(CScriptBase*)(&scriptPubKey));
    }

    void SetNull()
    {
        nValue = -1;
        scriptPubKey.clear();
    }

    bool IsNull() const
    {
        return (nValue == -1);
    }

    bool IsOpReturn() const {
       std::vector<uint8_t> vchR;
       opcodetype opCode;
       CScript scriptPK = this->scriptPubKey;
       CScript::const_iterator pc = scriptPK.begin();
       if (!vchR.empty()) {
             vchR.clear();
       }
       if (scriptPK.GetOp(pc, opCode, vchR) && (opCode == OP_RETURN)) {
              return true;
       }
       return false;
    }

    uint256 GetHash() const;

    CAmount GetDustThreshold(const CFeeRate &minRelayTxFee) const
    {
        // "Dust" is defined in terms of CTransaction::minRelayTxFee,
        // which has units satoshis-per-kilobyte.
        // If you'd pay more than 1/3 in fees
        // to spend something, then we consider it dust.
        // A typical spendable non-segwit txout is 34 bytes big, and will
        // need a CTxIn of at least 148 bytes to spend:
        // so dust is a spendable txout less than
        // 546*minRelayTxFee/1000 (in satoshis).
        // A typical spendable segwit txout is 31 bytes big, and will
        // need a CTxIn of at least 67 bytes to spend:
        // so dust is a spendable txout less than
        // 294*minRelayTxFee/1000 (in satoshis).
        if (scriptPubKey.IsUnspendable())
            return 0;

        size_t nSize = GetSerializeSize(*this, SER_DISK, 0);
        int witnessversion = 0;
        std::vector<unsigned char> witnessprogram;

        if (scriptPubKey.IsWitnessProgram(witnessversion, witnessprogram)) {
            // sum the sizes of the parts of a transaction input
            // with 75% segwit discount applied to the script size.
            nSize += (32 + 4 + 1 + (107 / WITNESS_SCALE_FACTOR) + 4);
        } else {
            nSize += (32 + 4 + 1 + 107 + 4); // the 148 mentioned above
        }

        return 3 * minRelayTxFee.GetFee(nSize);
    }

    bool IsDust(const CFeeRate &minRelayTxFee) const
    {
        return (nValue < GetDustThreshold(minRelayTxFee));
    }

    friend bool operator==(const CTxOut& a, const CTxOut& b)
    {
        return (a.nValue       == b.nValue &&
                a.scriptPubKey == b.scriptPubKey);
    }

    friend bool operator!=(const CTxOut& a, const CTxOut& b)
    {
        return !(a == b);
    }

    std::string ToString() const;
};

class CTxInWitness
{
public:
    CScriptWitness scriptWitness;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(scriptWitness.stack);
    }

    bool IsNull() const { return scriptWitness.IsNull(); }

    CTxInWitness() { }
};

class CTxWitness
{
public:
    /** In case vtxinwit is missing, all entries are treated as if they were empty CTxInWitnesses */
    std::vector<CTxInWitness> vtxinwit;

    ADD_SERIALIZE_METHODS;

    bool IsEmpty() const { return vtxinwit.empty(); }

    bool IsNull() const
    {
        for (size_t n = 0; n < vtxinwit.size(); n++) {
            if (!vtxinwit[n].IsNull()) {
                return false;
            }
        }
        return true;
    }

    void SetNull()
    {
        vtxinwit.clear();
    }

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        for (size_t n = 0; n < vtxinwit.size(); n++) {
            READWRITE(vtxinwit[n]);
        }
        if (IsNull()) {
            /* It's illegal to encode a witness when all vtxinwit entries are empty. */
            throw std::ios_base::failure("Superfluous witness record");
        }
    }
};

struct CMutableTransaction;

/**
 * Basic transaction serialization format:
 * - int32_t nVersion
 * - std::vector<CTxIn> vin
 * - std::vector<CTxOut> vout
 * - uint32_t nLockTime
 *
 * Extended transaction serialization format:
 * - int32_t nVersion
 * - unsigned char dummy = 0x00
 * - unsigned char flags (!= 0)
 * - std::vector<CTxIn> vin
 * - std::vector<CTxOut> vout
 * - if (flags & 1):
 *   - CTxWitness wit;
 * - uint32_t nLockTime
 */
template<typename Stream, typename Operation, typename TxType>
inline void SerializeTransaction(TxType& tx, Stream& s, Operation ser_action) {
    const bool fAllowWitness = !(s.GetVersion() & SERIALIZE_TRANSACTION_NO_WITNESS);

    READWRITE(*const_cast<int32_t*>(&tx.nVersion));
    unsigned char flags = 0;
    if (ser_action.ForRead()) {
        const_cast<std::vector<CTxIn>*>(&tx.vin)->clear();
        const_cast<std::vector<CTxOut>*>(&tx.vout)->clear();
        const_cast<CTxWitness*>(&tx.wit)->SetNull();
        /* Try to read the vin. In case the dummy is there, this will be read as an empty vector. */
        READWRITE(*const_cast<std::vector<CTxIn>*>(&tx.vin));
        if (tx.vin.size() == 0 && fAllowWitness) {
            /* We read a dummy or an empty vin. */
            READWRITE(flags);
            if (flags != 0) {
                READWRITE(*const_cast<std::vector<CTxIn>*>(&tx.vin));
                READWRITE(*const_cast<std::vector<CTxOut>*>(&tx.vout));
            }
        } else {
            /* We read a non-empty vin. Assume a normal vout follows. */
            READWRITE(*const_cast<std::vector<CTxOut>*>(&tx.vout));
        }
        if ((flags & 1) && fAllowWitness) {
            /* The witness flag is present, and we support witnesses. */
            flags ^= 1;
            const_cast<CTxWitness*>(&tx.wit)->vtxinwit.resize(tx.vin.size());
            READWRITE(tx.wit);
        }
        if (flags) {
            /* Unknown flag in the serialization */
            throw std::ios_base::failure("Unknown transaction optional data");
        }
    } else {
        // Consistency check
        assert(tx.wit.vtxinwit.size() <= tx.vin.size());
        if (fAllowWitness) {
            /* Check whether witnesses need to be serialized. */
            if (!tx.wit.IsNull()) {
                flags |= 1;
            }
        }
        if (flags) {
            /* Use extended format in case witnesses are to be serialized. */
            std::vector<CTxIn> vinDummy;
            READWRITE(vinDummy);
            READWRITE(flags);
        }
        READWRITE(*const_cast<std::vector<CTxIn>*>(&tx.vin));
        READWRITE(*const_cast<std::vector<CTxOut>*>(&tx.vout));
        if (flags & 1) {
            const_cast<CTxWitness*>(&tx.wit)->vtxinwit.resize(tx.vin.size());
            READWRITE(tx.wit);
        }
    }
    READWRITE(*const_cast<uint32_t*>(&tx.nLockTime));
}

/** The basic transaction that is broadcasted on the network and contained in
 * blocks.  A transaction can contain multiple inputs and outputs.
 */
class CTransaction
{
private:
    /** Memory only. */
    const uint256 hash;

public:
    // Default transaction version.

    // versions
    //    1 : launch
    //    2 : after pivot
    static const int CURRENT_VERSION = 2;
    static const int PREVIOUS_VERSION = 1;
    // static const int32_t CURRENT_VERSION=1;
    // static const int PREVIOUS_VERSION = 1;
    static const int32_t NAMECOIN_VERSION=0x7100; // 28928

    // Changing the default transaction version requires a two step process: first
    // adapting relay policy by bumping MAX_STANDARD_VERSION, and then later date
    // bumping the default CURRENT_VERSION at which point both CURRENT_VERSION and
    // MAX_STANDARD_VERSION will be equal.
    static const int32_t MAX_STANDARD_VERSION=2;

    // The local variables are made const to prevent unintended modification
    // without updating the cached hash value. However, CTransaction is not
    // actually immutable; deserialization and assignment are implemented,
    // and bypass the constness. This is safe, as they update the entire
    // structure, including the hash.
    const int32_t nVersion;
    const std::vector<CTxIn> vin;
    const std::vector<CTxOut> vout;
    CTxWitness wit; // Not const: can change without invalidating the txid cache
    const uint32_t nLockTime;

    /** Construct a CTransaction that qualifies as IsNull() */
    CTransaction();

    /** Convert a CMutableTransaction into a CTransaction. */
    CTransaction(const CMutableTransaction &tx);
    CTransaction(CMutableTransaction &&tx);

    CTransaction& operator=(const CTransaction& tx);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        SerializeTransaction(*this, s, ser_action);
        if (ser_action.ForRead()) {
            UpdateHash();
    }
    }

    template <typename Stream>
    CTransaction(deserialize_type, Stream& s) : CTransaction(CMutableTransaction(deserialize, s)) {}

    bool IsNull() const {
        return vin.empty() && vout.empty();
    }

    const uint256& GetHash() const {
        return hash;
    }

    // Compute a hash that includes both transaction and witness data
    uint256 GetWitnessHash() const;

    // Return sum of txouts.
    CAmount GetValueOut(bool fExclueNames = false) const;
    // GetValueIn() is a method on CCoinsViewCache, because
    // inputs must be known to compute value in.

    // Compute priority, given priority of inputs and (optionally) tx size
    double ComputePriority(double dPriorityInputs, unsigned int nTxSize=0) const;

    // Compute modified tx size for priority calculation (optionally given tx size)
    unsigned int CalculateModifiedSize(unsigned int nTxSize=0) const;
    
    /**
     * Get the total transaction size in bytes, including witness data.
     * "Total Size" defined in BIP141 and BIP144.
     * @return Total transaction size in bytes
     */
    unsigned int GetTotalSize() const;

    bool IsCoinBase() const
    {
        return (vin.size() == 1 && vin[0].prevout.IsNull());
    }

    bool IsNamecoin() const
    {
        return nVersion == NAMECOIN_VERSION;
    }

    friend bool operator==(const CTransaction& a, const CTransaction& b)
    {
        return a.hash == b.hash;
    }

    friend bool operator!=(const CTransaction& a, const CTransaction& b)
    {
        return a.hash != b.hash;
    }

    std::string ToString() const;
    int64_t GetOpRetFee() const;
    void UpdateHash() const;
};

/** A mutable version of CTransaction. */
struct CMutableTransaction
{
    int32_t nVersion;
    std::vector<CTxIn> vin;
    std::vector<CTxOut> vout;
    CTxWitness wit;
    uint32_t nLockTime;

    CMutableTransaction();
    CMutableTransaction(const CTransaction& tx);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        SerializeTransaction(*this, s, ser_action);
        }

    template <typename Stream>
    CMutableTransaction(deserialize_type, Stream& s) {
        Unserialize(s);
    }

    /** Compute the hash of this CMutableTransaction. This is computed on the
     * fly, as opposed to GetHash() in CTransaction, which uses a cached result.
     */
    uint256 GetHash() const;


    /** Get Inscription Fees
    */
    int64_t GetOpRetFee() const;

    /**
     * Turn this into a Namecoin version transaction.  It is assumed
     * that it isn't already.
     */
    void SetNamecoin();
};

typedef std::shared_ptr<const CTransaction> CTransactionRef;
static inline CTransactionRef MakeTransactionRef() { return std::make_shared<const CTransaction>(); }
template <typename Tx> static inline CTransactionRef MakeTransactionRef(Tx&& txIn) { return std::make_shared<const CTransaction>(std::forward<Tx>(txIn)); }
static inline CTransactionRef MakeTransactionRef(const CTransactionRef& txIn) { return txIn; }
static inline CTransactionRef MakeTransactionRef(CTransactionRef&& txIn) { return std::move(txIn); }

/** Compute the weight of a transaction, as defined by BIP 141 */
int64_t GetTransactionWeight(const CTransaction &tx);

#endif // BITCOIN_PRIMITIVES_TRANSACTION_H
