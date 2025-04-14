#include <iostream>

// ���N���X
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

// ���z�h���N���X1
class Derived1 : virtual public Base {
public:
    using Base::Base;
};

// ���z�h���N���X2
class Derived2 : virtual public Base {
public:
    using Base::Base;
};

// �ł��h�������N���X
class MostDerived : public Derived1, public Derived2 {
public:
    MostDerived(int value) : Base(value) { }
};

void test29() {
    // MostDerived �𐶐�
    MostDerived obj(42); // �R���X�g���N�^������ Base �֓n��
}
