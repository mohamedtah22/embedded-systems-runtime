FROM python:3.12-slim
WORKDIR /app
COPY pipeline/ /app/pipeline/
CMD ["sh", "-c", "python pipeline/generate_demo.py --output /shared/events.csv --notes /shared/notes.jsonl --lots 50 && python pipeline/transform.py --input /shared/events.csv --notes /shared/notes.jsonl --output /shared/snapshot.json"]
