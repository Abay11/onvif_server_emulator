#pragma once

#include "IPhysicalComponent.h"

#include <string>
#include <memory>
#include <vector>

class IDigitalInput : public IPhysicalComponent
{
public:

    void Enable() override;
    void Disable() override;
    bool IsEnabled() override
    {
        return is_enabled_;
    }
    bool GetState() override
    {
        return state_;
    }
    bool SetState(bool state) override
    {
        return state_ = state;
    }

    bool InvertState() override;

    void SetToken(std::string str) { token_ = str; };

    std::string GetToken()
    {
        return token_;
    }

protected:
    std::string token_;

    bool state_ = false;

    bool is_enabled_ = true;

private:
};


class SimpleDigitalInputImpl : public IDigitalInput
{
public:
    SimpleDigitalInputImpl(const std::string& token, bool state = false)
    {
        token_ = token;
        state_ = state;
    }

    // Here no specific realization, but in future it may be need
    // To implement some features
};

using DigitalInputsList = std::vector<std::shared_ptr< IDigitalInput>>;
