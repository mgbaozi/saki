import json
import unittest
from pathlib import Path

from jsonschema import Draft202012Validator, FormatChecker
from jsonschema.exceptions import ValidationError
from referencing import Registry, Resource

REPO_ROOT = Path(__file__).resolve().parents[2]
SCHEMA_ROOT = REPO_ROOT / "protocol" / "schema" / "v1"
FIXTURE_ROOT = REPO_ROOT / "protocol" / "fixtures" / "v1"


def load_json(path: Path) -> dict[str, object]:
    with path.open(encoding="utf-8") as stream:
        return json.load(stream)


def message_validator() -> Draft202012Validator:
    schemas = [load_json(path) for path in SCHEMA_ROOT.glob("*.schema.json")]
    registry = Registry().with_resources(
        (schema["$id"], Resource.from_contents(schema)) for schema in schemas
    )
    messages = next(schema for schema in schemas if schema["$id"] == "messages.schema.json")
    return Draft202012Validator(messages, registry=registry, format_checker=FormatChecker())


class SchemaTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validator = message_validator()

    def test_valid_fixtures(self) -> None:
        for path in sorted((FIXTURE_ROOT / "valid").glob("*.json")):
            with self.subTest(path=path.name):
                self.validator.validate(load_json(path))

    def test_invalid_fixtures(self) -> None:
        for path in sorted((FIXTURE_ROOT / "invalid").glob("*.json")):
            with self.subTest(path=path.name), self.assertRaises(ValidationError):
                self.validator.validate(load_json(path))

    def test_session_fixtures(self) -> None:
        for path in sorted((FIXTURE_ROOT / "sessions").glob("*.ndjson")):
            with path.open(encoding="utf-8") as stream:
                for line_number, line in enumerate(stream, start=1):
                    with self.subTest(path=path.name, line=line_number):
                        self.validator.validate(json.loads(line))


if __name__ == "__main__":
    unittest.main()
