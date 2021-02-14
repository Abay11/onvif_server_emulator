#pragma once

class IPhysicalComponent
{
    virtual void Enable() = 0;

    virtual void Disable() = 0;

    virtual bool IsEnabled() = 0;

    /**
     * true - if a component is activated
     * false - if it is not activated
     */
    virtual bool GetState() = 0;

    /**
     * set a new state and return it
     */
    virtual bool SetState(bool state) = 0;

    /**
     * invert the current state and return the new one
     */
    virtual bool InvertState() = 0;
};
