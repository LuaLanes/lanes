#pragma once

// #################################################################################################

// A unique type generator
template <typename T, auto = [] {}, typename specialization = void>
class Unique
{
    private:
    T val;

    public:
    using type = T;
    Unique() = default;
    operator T() const { return val; }
    explicit Unique(T b_)
    : val{ b_ }
    {
    }
    // pre-imcrement
    auto& operator++()
    {
        ++val;
        return *this;
    }
    // post-increment
    auto operator++(int)
    {
        return Unique<T>{ std::exchange(val, val + 1) };
    }
};

template <typename T, auto lambda>
class Unique<T, lambda, std::enable_if_t<!std::is_scalar_v<T>>>
: public T
{
    public:
    using type = T;
    using T::T;
    explicit Unique(T const& b_)
    : T{ b_ }
    {
    }
};
