#include <iostream>

#include "bcrypt.h"

namespace
{

bool TestHashAndCheck()
{
    const char *password = "JobPilotAI@123456";

    char salt[BCRYPT_HASHSIZE]{};
    char hash[BCRYPT_HASHSIZE]{};

    if (bcrypt_gensalt(12, salt) != 0)
    {
        std::cerr << "bcrypt_gensalt failed\n";
        return false;
    }

    if (bcrypt_hashpw(password, salt, hash) != 0)
    {
        std::cerr << "bcrypt_hashpw failed\n";
        return false;
    }

    if (hash[0] == '\0')
    {
        std::cerr << "bcrypt hash is empty\n";
        return false;
    }

    if (bcrypt_checkpw(password, hash) != 0)
    {
        std::cerr << "correct password verification failed\n";
        return false;
    }

    if (bcrypt_checkpw("wrong-password", hash) == 0)
    {
        std::cerr << "wrong password was accepted\n";
        return false;
    }

    std::cout << "bcrypt hash/check test passed\n";
    return true;
}

bool TestDifferentSaltProducesDifferentHash()
{
    const char *password = "JobPilotAI@123456";

    char salt1[BCRYPT_HASHSIZE]{};
    char salt2[BCRYPT_HASHSIZE]{};

    char hash1[BCRYPT_HASHSIZE]{};
    char hash2[BCRYPT_HASHSIZE]{};

    if (bcrypt_gensalt(12, salt1) != 0 ||
        bcrypt_gensalt(12, salt2) != 0)
    {
        std::cerr << "bcrypt_gensalt failed\n";
        return false;
    }

    if (bcrypt_hashpw(password, salt1, hash1) != 0 ||
        bcrypt_hashpw(password, salt2, hash2) != 0)
    {
        std::cerr << "bcrypt_hashpw failed\n";
        return false;
    }

    if (std::string(hash1) == std::string(hash2))
    {
        std::cerr
            << "different salts produced identical hashes\n";
        return false;
    }

    if (bcrypt_checkpw(password, hash1) != 0 ||
        bcrypt_checkpw(password, hash2) != 0)
    {
        std::cerr
            << "password verification failed\n";
        return false;
    }

    std::cout << "bcrypt different-salt test passed\n";
    return true;
}

} // namespace

int main()
{
    if (!TestHashAndCheck())
    {
        return 1;
    }

    if (!TestDifferentSaltProducesDifferentHash())
    {
        return 1;
    }

    std::cout << "bcrypt test passed\n";
    return 0;
}
