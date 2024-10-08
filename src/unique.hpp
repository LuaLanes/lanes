#pragma once

// #################################################################################################

// A unique type generator
template <typename T, typename TAG, typename specialization = void>
class Unique
{
    private:
    T val;

    public:
    using type = T;
    constexpr Unique() = default;
    operator T() const { return val; }
    Unique& operator=(T const&) = delete;
    Unique& operator=(T&&) = delete;
    explicit constexpr Unique(T b_)
    : val{ b_ }
    {
    }
    // pre-increment
    auto& operator++()
    {
        ++val;
        return *this;
    }
    // post-increment
    auto operator++(int)
    {
        return Unique<T, TAG>{ std::exchange(val, val + 1) };
    }
};

template <typename T, typename TAG>
class Unique<T, TAG, std::enable_if_t<!std::is_scalar_v<T>>>
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

#define DECLARE_UNIQUE_TYPE(_name, _type) using _name = Unique<_type, class Unique_##_name##_Tag>

// putting this here to break a header circular dependency until I find a better place
DECLARE_UNIQUE_TYPE(StackIndex, int);
static constexpr StackIndex kIdxTop{ -1 };
