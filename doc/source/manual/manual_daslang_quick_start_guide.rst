Daslang Quick Start Guide
=========================

This guide is a fast, language-focused introduction to **daslang** for people
who already program in C, C++ or similar languages. It deliberately avoids
engine concepts -- the goal is to make you comfortable with the *language* so
that the ECS Programming section (which builds on these exact constructs) reads
naturally.

daslang is a statically typed, natively compiled language with a modern
gen2 syntax that should feel familiar: curly braces for blocks, parentheses
around conditions, and strong typing with type inference. Enable gen2 at the
top of every file with ``options gen2``.

Hello, World
------------

A runnable daslang program needs an exported ``main``. ``print`` writes to the
console (add ``\n`` for a newline), and ``{expr}`` inside a string is
interpolated.

.. list-table::
   :header-rows: 1

   * - Daslang
     - C++
   * - .. code-block:: das

         options gen2

         [export]
         def main {
             print("Hello, World!\n")
             let name = "daslang"
             print("Welcome to {name}!\n")
         }
     - .. code-block:: cpp

         #include <iostream>

         int main() {
             std::cout << "Hello, World!\n";
             std::string name = "daslang";
             std::cout << "Welcome to " << name << "!\n";
         }

Variables and Types
-------------------

Use ``var`` for mutable variables and ``let`` for immutable ones. Types are
inferred from the initializer, but you can annotate them explicitly with
``: Type``. daslang is strict: there are **no implicit conversions** between
``int`` and ``float`` (cast explicitly with ``float(x)`` or ``int(x)``).

.. list-table::
   :header-rows: 1

   * - Daslang
     - C++
   * - .. code-block:: das

         var score = 0          // mutable int
         score = 100

         let maxScore = 999     // immutable int
         // maxScore = 0        // ERROR: cannot modify

         let i = 42             // inferred int
         let f : float = 3.14   // explicit float
         let b : bool = true
         let s = "hello"        // inferred string
         let h = 0xFF           // inferred uint
         let big : int64 = 9_000_000_000l
     - .. code-block:: cpp

         int score = 0;         // mutable
         score = 100;

         const int maxScore = 999;  // immutable
         // maxScore = 0;           // ERROR: cannot modify

         int i = 42;
         float f = 3.14f;
         bool b = true;
         std::string s = "hello";
         unsigned int h = 0xFF;
         int64_t big = 9000000000LL;

Functions
---------

Functions are declared with ``def``. Parameter and return types are written
with ``:`` (or ``->``); the return type can be omitted when it is inferred. A
function whose body is a single expression can use the arrow form ``=>``.
Default and named arguments are supported.

.. list-table::
   :header-rows: 1

   * - Daslang
     - C++
   * - .. code-block:: das

         def add(a, b : int) : int {
             return a + b
         }

         // single-expression body
         def succ(x : int) : int => x + 1

         // default + named arguments
         def test(a, b : int; c : int = 1) : int {
             return a + b + c
         }
         // test([a = 2, b = 3])  -> 6
     - .. code-block:: cpp

         int add(int a, int b) {
             return a + b;
         }

         // single-expression body
         auto succ(int x) -> int { return x + 1; }

         // default + named arguments (C++20)
         int test(int a, int b, int c = 1) {
             return a + b + c;
         }
         // test(.a = 2, .b = 3)  -> 6

Functions can be passed around as first-class values via ``@@name``:

.. code-block:: das

   def twice(a : int) : int { return a + a }
   let fn = @@twice          // function pointer
   let t = fn(21)            // 42

Control Flow
------------

``if``/``elif``/``else`` and ``while`` work as expected, and the condition must
be a ``bool``. Iterate with ``for ... in range(...)`` or over any container.
``with (x) { ... }`` brings a struct's fields into scope.

.. list-table::
   :header-rows: 1

   * - Daslang
     - C++
   * - .. code-block:: das

         if (a > b) {
             a = b
         } elif (a < b) {
             b = a
         } else {
             print("equal\n")
         }

         for (i in range(0, 10)) {
             print("{i}\n")
         }

         while (running) {
             step()
         }
     - .. code-block:: cpp

         if (a > b) {
             a = b;
         } else if (a < b) {
             b = a;
         } else {
             std::cout << "equal\n";
         }

         for (int i = 0; i < 10; ++i) {
             std::cout << i << "\n";
         }

         while (running) {
             step();
         }

Structs
-------

``struct`` groups data (no member functions by default). An initializer with
the same name as the struct fills fields. "Methods" are just free functions
called with the pipe operator ``|>`` (or ``def name`` inside the struct).
Structs support single inheritance with ``:``.

.. list-table::
   :header-rows: 1

   * - Daslang
     - C++
   * - .. code-block:: das

         struct Vec2 {
             x, y : float = 0.0
         }

         def length(v : Vec2) : float {
             return sqrt(v.x * v.x + v.y * v.y)
         }

         var v = Vec2(x = 3.0, y = 4.0)
         let len = v |> length()     // 5.0
     - .. code-block:: cpp

         struct Vec2 {
             float x = 0.0f;
             float y = 0.0f;
         };

         float length(const Vec2 &v) {
             return std::sqrt(v.x * v.x + v.y * v.y);
         }

         Vec2 v{3.0f, 4.0f};
         float len = length(v);      // 5.0f

Enums
-----

Enumerations declare named integer constants:

.. list-table::
   :header-rows: 1

   * - Daslang
     - C++
   * - .. code-block:: das

         enum Color {
             Red
             Green
             Blue
         }
         let c = Color Green
     - .. code-block:: cpp

         enum class Color {
             Red,
             Green,
             Blue,
         };
         Color c = Color::Green;

Arrays
------

Dynamic arrays (``array<T>``) grow on the heap and are passed by reference;
fixed-size arrays (``float[4]``) live on the stack. Build them inline with
``[ ... ]`` (moved with ``<-``) and append with ``push`` or the pipe form
``|> push``.

.. list-table::
   :header-rows: 1

   * - Daslang
     - C++
   * - .. code-block:: das

         var nums : array<int>
         nums |> push(1)
         nums |> push(2)

         let fixed = [1, 2, 3, 4]      // moved into `pts`
         for (n in fixed) {
             print("{n}\n")
         }
         let sub <- fixed[1..3]        // [2, 3]
     - .. code-block:: cpp

         std::vector<int> nums;
         nums.push_back(1);
         nums.push_back(2);

         std::array<int,4> fixed{1,2,3,4};
         for (int n : fixed) {
             std::cout << n << "\n";
         }
         // sub-range: need std::vector + copy

References and Pointers
-----------------------

References (``T&``) and nullable pointers (``T?``) are central to daslang and
will matter a lot when you read components back from the ECS later. A reference
aliases an existing value; a nullable pointer may be ``null``. Allocate with
``new`` (returns ``T?``) and free with ``delete`` (unsafe). Dereference with
``*p``; struct fields auto-dereference (``p.x``). ``?.`` is null-safe
navigation and ``??`` is null-coalescing.

.. list-table::
   :header-rows: 1

   * - Daslang
     - C++
   * - .. code-block:: das

         var n = 42
         var r : int& = n         // reference to n
         r = 100                  // n is now 100

         var p : Vec2? = new Vec2(x = 1.0)
         print("{p.x}\n")         // auto-deref
         let safe = p?.x ?? 0.0   // 0.0 if p is null
         unsafe { delete p }       // free, p -> null
     - .. code-block:: cpp

         int n = 42;
         int &r = n;              // reference to n
         r = 100;                 // n is now 100

         Vec2 *p = new Vec2{1.0f};
         std::cout << p->x << "\n";
         float safe = p ? p->x : 0.0f;
         delete p;                // p -> dangling

Lambdas and the Pipe Operator
-----------------------------

Anonymous functions ("lambdas") are written with ``@``. They can capture local
variables. The pipe operator ``|>`` feeds a value into the first argument of a
call -- a common idiom for chaining work:

.. list-table::
   :header-rows: 1

   * - Daslang
     - C++
   * - .. code-block:: das

         let counter <- @ (extra : int) : int {
             return extra + 1
         }
         let t = counter(41)      // 42

         var nums : array<int>
         nums |> push(1) |> push(2)
     - .. code-block:: cpp

         auto counter = [](int extra) -> int {
             return extra + 1;
         };
         int t = counter(41);     // 42

         std::vector<int> nums;
         nums.push_back(1);
         nums.push_back(2);

Modules and ``require``
------------------------

Code is organized into modules. A file declares its module with ``module name``
and pulls in others with ``require``. Modules are how you will later bring in
the engine's own bindings (for example ``require weasel`` in game code), but the mechanism
itself is plain language infrastructure:

.. code-block:: das

   module my_game

   require math            // built-in module
   require daslib/defer    // daslib module

   def public hello {
       print("{sin(0.0)}\n")
   }

This covers the language surface you need before writing systems and
components. The next section, ECS Programming, shows how these same constructs
are used to declare components and implement systems.
