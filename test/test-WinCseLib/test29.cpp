#include <iostream>

// 基底クラス
class Base {
public:
    Base() : value_(0) { }
    Base(int value) : value_(value) {
        std::cout << "Base class constructed with value: " << value_ << std::endl;
    }
    virtual ~Base() = default;

private:
    int value_;
};

// 仮想派生クラス1
class Derived1 : virtual public Base {
public:
    using Base::Base;
};

// 仮想派生クラス2
class Derived2 : virtual public Base {
public:
    using Base::Base;
};

// 最も派生したクラス
class MostDerived : public Derived1, public Derived2 {
public:
    MostDerived(int value) : Base(value) { }
};

void test29() {
    // MostDerived を生成
    MostDerived obj(42); // コンストラクタ引数を Base へ渡す
}
