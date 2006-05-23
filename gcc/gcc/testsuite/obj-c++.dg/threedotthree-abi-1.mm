/* APPLE LOCAL begin radar 4301047 */
#include <stdio.h>
#include <string.h>
#include <objc/Protocol.h>
#ifndef __NEXT_RUNTIME__
#include <objc/objc-api.h>
#endif

extern "C" void abort();


@protocol CommonProtocol

-(oneway void)methodCall_On:(in bycopy id)someValue_On;
-(oneway void)methodCall_nO:(bycopy in id)someValue_nO;

-(oneway void)methodCall_Oo:(out bycopy id)someValue_Oo;
-(oneway void)methodCall_oO:(bycopy out id)someValue_oO;

-(oneway void)methodCall_rn:(in const id)someValue_rn;

-(oneway void)methodCall_oOn:(in bycopy out id)someValue_oOn;

@end

@interface ObjCClass <CommonProtocol>
{

}

@end

@implementation ObjCClass
-(oneway void)methodCall_On:(in bycopy id)someValue_On { }
-(oneway void)methodCall_nO:(bycopy in id)someValue_nO { }

-(oneway void)methodCall_Oo:(out bycopy id)someValue_Oo { }
-(oneway void)methodCall_oO:(bycopy out id)someValue_oO { }

-(oneway void)methodCall_rn:(in const id)someValue_rn { }
-(oneway void)methodCall_oOn:(in bycopy out id)someValue_oOn { }
@end

Protocol *proto = @protocol(CommonProtocol);
struct objc_method_description *meth;

int main()
{
        meth = [proto descriptionForInstanceMethod: @selector(methodCall_On:)];
	if (strcmp (meth->types, "Vv12@0:4On@8"))
	  abort();
        meth = [proto descriptionForInstanceMethod: @selector(methodCall_nO:)];
	if (strcmp (meth->types, "Vv12@0:4nO@8"))
	  abort();
        meth = [proto descriptionForInstanceMethod: @selector(methodCall_Oo:)];
	if (strcmp (meth->types, "Vv12@0:4Oo@8"))
	  abort();
        meth = [proto descriptionForInstanceMethod: @selector(methodCall_oO:)];
	if (strcmp (meth->types, "Vv12@0:4oO@8"))
	  abort();
        meth = [proto descriptionForInstanceMethod: @selector(methodCall_rn:)];
	if (strcmp (meth->types, "Vv12@0:4rn@8"))
	  abort();
        meth = [proto descriptionForInstanceMethod: @selector(methodCall_oOn:)];
	if (strcmp (meth->types, "Vv12@0:4oOn@8"))
	  abort();
	return 0;
}
/* APPLE LOCAL end radar 4301047 */
