from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import importlib.util
import io
import json
from pathlib import Path
import sys
import threading
import unittest
from unittest.mock import patch
import urllib.error

from scripts import i18n_android


ROOT = Path(__file__).resolve().parents[2]


class _RecordingHandler(BaseHTTPRequestHandler):
    def do_POST(self) -> None:
        content_length = int(self.headers["Content-Length"])
        body = json.loads(self.rfile.read(content_length))
        self.server.requests.append(  # type: ignore[attr-defined]
            (self.path, self.headers["Authorization"], self.headers["User-Agent"], body)
        )
        response = json.dumps(
            {"choices": [{"message": {"content": '{"hello":"Bonjour"}'}}]}
        ).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(response)))
        self.end_headers()
        self.wfile.write(response)

    def log_message(self, format: str, *args: object) -> None:
        pass


def _load_desktop_translator():
    path = ROOT / "scripts" / "i18n-translate.py"
    spec = importlib.util.spec_from_file_location("dao_desktop_i18n", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class DefaultTranslationModelTest(unittest.TestCase):
    def test_http_errors_include_provider_details_without_exposing_the_api_key(self) -> None:
        for translator in (_load_desktop_translator(), i18n_android):
            for status in (403, 500):
                with self.subTest(translator=translator.__name__, status=status):
                    error = urllib.error.HTTPError(
                        "https://example.test/chat/completions", status, "Denied", {},
                        io.BytesIO(b'{"error":"model_access_denied for test-secret"}'),
                    )
                    with patch.object(translator.urllib.request, "urlopen", side_effect=error) as request, \
                         patch.object(translator.time, "sleep"):
                        with self.assertRaises(RuntimeError) as caught:
                            translator.call_openai("translate", "test-secret", "test-model")
                    message = str(caught.exception)
                    self.assertIn(str(status), message)
                    self.assertIn("model_access_denied", message)
                    self.assertNotIn("test-secret", message)
                    self.assertEqual(
                        3 if translator is i18n_android and status == 500 else 1,
                        request.call_count,
                    )

    def test_both_translators_send_gpt_5_5_to_chat_completions(self) -> None:
        server = ThreadingHTTPServer(("127.0.0.1", 0), _RecordingHandler)
        server.requests = []  # type: ignore[attr-defined]
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        endpoint = f"http://127.0.0.1:{server.server_port}"
        desktop = _load_desktop_translator()
        previous_android_url = i18n_android.OPENAI_BASE_URL
        try:
            i18n_android.OPENAI_BASE_URL = endpoint
            desktop.OPENAI_BASE_URL = endpoint

            self.assertEqual(
                '{"hello":"Bonjour"}',
                i18n_android.call_openai(
                    "translate",
                    "test-secret",
                    i18n_android.OPENAI_MODEL,
                ),
            )
            self.assertEqual(
                '{"hello":"Bonjour"}',
                desktop.call_openai(
                    "translate",
                    "test-secret",
                    desktop.OPENAI_MODEL,
                ),
            )
        finally:
            i18n_android.OPENAI_BASE_URL = previous_android_url
            server.shutdown()
            server.server_close()
            thread.join(timeout=2)

        self.assertEqual(2, len(server.requests))  # type: ignore[attr-defined]
        for path, authorization, user_agent, body in server.requests:  # type: ignore[attr-defined]
            self.assertEqual("/chat/completions", path)
            self.assertEqual("Bearer test-secret", authorization)
            self.assertEqual("dao-i18n/1.0", user_agent)
            self.assertEqual("gpt-5.5", body["model"])
            self.assertEqual({"type": "json_object"}, body["response_format"])


if __name__ == "__main__":
    unittest.main()
