// Check -gstabs 
// Contributed by Devang Patel  dpatel@apple.com
// { dg-do compile }
class LcBase
{
public:
   virtual ~LcBase();
protected:
   LcBase();
};

class LcDerive : public LcBase
{
public:
   LcDerive();
   ~LcDerive();
};

LcDerive::LcDerive()
: LcBase()
{
}

LcDerive::~LcDerive()
{
}

