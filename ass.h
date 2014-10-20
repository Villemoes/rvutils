#ifndef ASS_H
#define ASS_H

#include <assert.h>

/*
 * Assertions which are true when testing should also be true in real
 * life. Do not disable assertions. And if you do, make sure to work
 * around the bug in the C standard which says that "assert(e)" may
 * expand to "((void)0)", when it would have been much much much
 * better to have it expand to "((void) (e))". That allows e to have
 * side effects, and if it does not, any non-toy compiler will
 * optimize the expression away, while the (void) cast stops it from
 * complaining about a statement with no effects.
 */

#ifdef NDEBUG
#error "You're doing it wrong!"
#undef assert
#define assert(e) ((void) (e))
#endif


#define AZ(e)  assert((e) == 0)
#define AN(e)  assert((e) != 0)

/* gcc has _Static_assert since at least 4.6, and Clang has it for a couple of years. */
#ifndef static_assert
#define static_assert _Static_assert
#endif

static_assert(1, "WTF? 1 is supposed to be true");

/**
 * static_assert_zero(e, text) - static assertion in expression context
 *
 * @e: The expression which must be (compile-time constant and) true
 * @text: A valid C identifier consisting of meaningful text
 *
 * It is sometimes useful to use static assertions in places where a
 * declaration is not possible, for example to check that macro
 * arguments satisfy certain criteria. When the expansion of the macro
 * is supposed to be a constant expression (e.g. used in static
 * initialization), we cannot use a statement expression.
 *
 * The macro static_assert_zero(e, text) evaluates to a constant
 * expression with value 0 if @e is true; otherwise, it causes a
 * compilation failure, and the compiler, hopefully, emits a message
 * containing @text.
 *
 * The Linux kernel contains a similar macro with an equally confusing
 * name, BUILD_BUG_ON_ZERO - we are not asserting that @e is zero.
 */

/*
 * We rely on a named zero-width bit field being illegal. It currently
 * works on gcc and clang, and
 * https://gcc.gnu.org/wiki/HowToPrepareATestcase even shows it as an
 * example of how to write an "expected diagnostics" test.
 *
 * We cast to (int) so that the expression has the same type as a bare
 * 0, in order not to cause unwanted promotions of other parts of the
 * enclosing expression.
 */
#define static_assert_zero(e, text)		\
	((int)(0*sizeof(struct { unsigned text:!!(e); })))

static_assert(static_assert_zero(1, self_test) == 0, "The macro static_assert_zero should evaluate to 0");


#endif /* ASS_H */
