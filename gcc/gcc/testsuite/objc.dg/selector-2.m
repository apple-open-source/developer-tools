/* Test that we don't ICE when issuing a -Wselector warning.  */
/* APPLE LOCAL Objective-C */
/* { dg-options "-Wselector" } */
/* { dg-do compile } */

#include <objc/Object.h>

@interface Foo
@end
@implementation Foo
-(void) foo
{
  SEL a;
  /* APPLE LOCAL Objective-C */
  a = @selector(b1ar); /* { dg-warning "creating selector for nonexistent method .b1ar." } */
}
/* APPLE LOCAL Objective-C */
@end
