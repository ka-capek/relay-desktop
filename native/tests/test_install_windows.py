"""Exercise installer data preservation without touching an installed app."""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("install_windows", Path(__file__).parents[1] / "tools/install_windows.py")
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class WindowsInstallerTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.stage = self.root / "stage"
        self.stage.mkdir()
        (self.stage / "Relay.exe").write_bytes(b"new app")
        self.destination = self.root / "Relay Native"

    def old_app(self):
        self.destination.mkdir()
        (self.destination / "Relay.exe").write_bytes(b"old app")

    def test_crt_supports_active_visual_studio_toolsets(self):
        for name in ("Microsoft.VC143.CRT", "Microsoft.VC145.CRT"):
            with self.subTest(name=name):
                root = self.root / name
                crt = root / "x64" / name
                crt.mkdir(parents=True)
                (crt / "msvcp140.dll").touch()
                (crt / "vcruntime140.dll").touch()
                self.assertEqual(installer.release_crt_directory(root), crt)

    def test_crt_refuses_incomplete_or_ambiguous_payload(self):
        root = self.root / "redist"
        for name in ("Microsoft.VC143.CRT", "Microsoft.VC145.CRT"):
            crt = root / "x64" / name
            crt.mkdir(parents=True)
            (crt / "msvcp140.dll").touch()
        with self.assertRaises(RuntimeError):
            installer.release_crt_directory(root)
        for crt in (root / "x64").iterdir():
            (crt / "vcruntime140.dll").touch()
        with self.assertRaises(RuntimeError):
            installer.release_crt_directory(root)

    def test_first_install(self):
        self.assertIsNone(installer.install_staged(self.stage, self.destination))
        self.assertEqual((self.destination / "Relay.exe").read_bytes(), b"new app")

    def test_registered_installer_is_not_replaced_by_source_build(self):
        self.old_app()
        (self.destination / "relay-native-install.ini").write_text("registered")
        with self.assertRaisesRegex(RuntimeError, "Setup executable"):
            installer.install_staged(self.stage, self.destination)
        self.assertEqual((self.destination / "Relay.exe").read_bytes(), b"old app")

    def test_update_retains_old_app(self):
        self.old_app()
        backup = installer.install_staged(self.stage, self.destination)
        self.assertEqual((backup / "Relay.exe").read_bytes(), b"old app")
        self.assertEqual((self.destination / "Relay.exe").read_bytes(), b"new app")

    def test_publish_failure_restores_old_app(self):
        self.old_app()
        rename = Path.rename

        def fail_stage(path, target):
            if path == self.stage:
                raise PermissionError("Simulated locked destination")
            return rename(path, target)

        with patch.object(Path, "rename", fail_stage):
            with self.assertRaises(PermissionError):
                installer.install_staged(self.stage, self.destination)
        self.assertEqual((self.destination / "Relay.exe").read_bytes(), b"old app")
        self.assertTrue(self.stage.exists())

    def test_existing_unrelated_directory_is_preserved(self):
        self.destination.mkdir()
        (self.destination / "personal-file").write_text("preserve")
        with self.assertRaises(RuntimeError):
            installer.install_staged(self.stage, self.destination)
        self.assertEqual((self.destination / "personal-file").read_text(), "preserve")

    def test_incomplete_stage_does_not_move_old_app(self):
        self.old_app()
        (self.stage / "Relay.exe").unlink()
        with self.assertRaises(RuntimeError):
            installer.install_staged(self.stage, self.destination)
        self.assertEqual((self.destination / "Relay.exe").read_bytes(), b"old app")

    def test_smoke_cannot_use_sdk_path_or_plugins(self):
        with patch.dict(os.environ, {"WINDIR": "C:/Windows", "PATH": "C:/Qt/bin",
                                     "QT_PLUGIN_PATH": "C:/Qt/plugins", "QT_QPA_PLATFORM": "offscreen"}):
            with patch.object(installer, "run") as run:
                installer.smoke_test(self.stage / "Relay.exe")
        environment = run.call_args.kwargs["env"]
        self.assertNotIn("C:/Qt", environment["PATH"])
        self.assertNotIn("QT_PLUGIN_PATH", environment)
        self.assertNotIn("QT_QPA_PLATFORM", environment)
        self.assertEqual(run.call_args.args[1], "--smoke-test")
        self.assertEqual(run.call_args.kwargs["timeout"], 30)

    def test_running_app_is_not_replaced(self):
        with patch.object(installer.subprocess, "check_output", return_value='"Relay.exe","1234","Console","1","50 K"\n'):
            with self.assertRaisesRegex(RuntimeError, "Quit Relay"):
                installer.require_app_closed()

    def test_localized_tasklist_without_relay_is_accepted(self):
        with patch.object(installer.subprocess, "check_output", return_value='INFORMACE: Žádné úlohy.\n'):
            installer.require_app_closed()

    def test_shortcut_path_is_data_not_powershell_code(self):
        destination = self.root / "Relay user's `folder` $name"
        with patch.object(installer, "run") as run:
            installer.create_start_menu_shortcut(destination)
        self.assertNotIn(str(destination), run.call_args.args[-1])
        self.assertEqual(run.call_args.kwargs["env"]["RELAY_INSTALL_TARGET"], str(destination / "Relay.exe"))


if __name__ == "__main__":
    unittest.main()
