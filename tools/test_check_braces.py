import contextlib
import io
from pathlib import Path
import tempfile
import unittest

from check_braces import main, source_files, unbraced_bodies


class BraceCheckTest(unittest.TestCase):
    def kinds(self, text, language):
        return [kind for kind, _, _ in unbraced_bodies(text.encode(), language)]

    def test_c_and_cpp_control_bodies(self):
        for language in ("c", "cpp"):
            with self.subTest(language=language):
                self.assertEqual(self.kinds("""
                    void f() {
                        if (x) f(); else if (y) f(); else f();
                        for (;;) f();
                        while (x) f();
                        do f(); while (x);
                    }
                """, language), ["if", "if", "else", "for", "while", "do"])

    def test_cpp_constexpr_range_and_nested_bodies(self):
        self.assertEqual(self.kinds("""
            void f() {
                if constexpr (ready) return;
                for (auto x : xs) if (x) f();
            }
        """, "cpp"), ["if", "for", "if"])

    def test_java_control_bodies(self):
        self.assertEqual(self.kinds("""
            class X { void f() {
                if (x) f(); else if (y) f(); else f();
                for (;;) f();
                for (X x : xs) f();
                while (x) f();
                do f(); while (x);
            } }
        """, "java"), ["if", "if", "else", "for", "for", "while", "do"])

    def test_braced_bodies_comments_and_literals_are_accepted(self):
        body = """
            if (x) { f(); } else if (y) { f(); } else { f(); }
            for (;;) { f(); }
            while (x) { f(); }
            do { f(); } while (x);
            // if (x) f(); for (;;) f(); while (x) f();
            /* if (x) f(); */
            const char *s = "if (x) f(); while (x) f();";
        """
        for language in ("c", "cpp"):
            with self.subTest(language=language):
                self.assertEqual(self.kinds("void f() {" + body + "}", language), [])
        self.assertEqual(self.kinds("""
            class X { void f() {
                if (x) { f(); } else if (y) { f(); } else { f(); }
                for (X x : xs) { f(); }
                while (x) { f(); }
                do { f(); } while (x);
                String s = "if (x) f();"; // while (x) f();
            } }
        """, "java"), [])

    def test_multiline_conditions_comments_and_empty_bodies(self):
        for language in ("c", "cpp"):
            with self.subTest(language=language):
                self.assertEqual(self.kinds("""
                    void f() {
                        if (ready(
                            x, nested(y))) /* explanation */
                            f();
                        while (ready()) ;
                        for (;;) ;
                    }
                """, language), ["if", "while", "for"])

    def test_cpp_attributes_and_preprocessor_branches(self):
        self.assertEqual(self.kinds("""
            void f() {
                if (x) [[likely]] { f(); }
            #ifdef FEATURE
                if (x) f();
            #else
                while (x) f();
            #endif
            }
        """, "cpp"), ["if", "while"])

    def test_scope_and_cli_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            sources = {
                "src/a.cpp": "void f() { if (x) f(); }",
                "src/nested/a.h": "inline void f() {}",
                "android/app/src/main/java/X.java": "class X {}",
                "android/app/src/test/java/XTest.java": "class XTest {}",
                "android/tests/XTest.java": "class XTest {}",
            }
            excluded = ["vendor/v.c", "android/app/build/X.java",
                        "android/app/.cxx/X.java", "android/app/src/main/cpp/v.cpp"]
            for name, content in {**sources, **dict.fromkeys(excluded, "ignored")}.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content)
            self.assertEqual({str(p.relative_to(root)) for p in source_files(root)}, set(sources))
            output = io.StringIO()
            with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
                self.assertEqual(main(["--root", str(root)]), 1)
            self.assertIn("src/a.cpp:1:12: error: if body must use braces", output.getvalue())
            (root / "src/a.cpp").write_text("void f() { if (x) { f(); } }")
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(main(["--root", str(root)]), 0)


if __name__ == "__main__":
    unittest.main()
