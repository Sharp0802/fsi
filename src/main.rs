use khroma;
use khroma::models::{
    AddCollectionRecordsPayload, CollectionConfiguration, CreateCollectionPayload,
    EmbeddingFunctionConfiguration, EmbeddingFunctionNewConfiguration, EmbeddingsPayload, Include,
    QueryRequestPayload,
};
use khroma::{Collection, Khroma, KhromaError};
use ollama_rs::Ollama;
use ollama_rs::generation::embeddings::request::{EmbeddingsInput, GenerateEmbeddingsRequest};
use reqwest::StatusCode;
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use std::collections::{HashMap, HashSet};
use std::env;
use std::io::{Read, stdin};
use std::path::PathBuf;
use tokio::fs::File;
use tokio::io::AsyncReadExt;
use tree_sitter::{Node, Parser};

const CHROMA_URL: &str = "http://localhost:8000";
const OLLAMA_URL: &str = "http://localhost:11434";
const EMBEDDING_MODEL: &str = "nomic-embed-text";
const DATABASE_NAME: &str = "rag";

#[derive(Debug, Serialize, Deserialize, Clone)]
struct Code {
    path: String,
    line: u64,
    code: String,
}

async fn add(
    ollama: &Ollama,
    collection: &Collection,
    offset: usize,
    docs: Vec<String>,
    meta: Vec<HashMap<String, Value>>,
) -> Result<(), Box<dyn std::error::Error>> {
    let request = GenerateEmbeddingsRequest::new(
        EMBEDDING_MODEL.to_string(),
        EmbeddingsInput::Multiple(docs.clone()),
    );
    let embeddings = ollama.generate_embeddings(request).await?;

    let payload = AddCollectionRecordsPayload {
        ids: (offset..(offset + docs.len()))
            .map(|i| i.to_string())
            .collect(),
        embeddings: Some(EmbeddingsPayload::Float(embeddings.embeddings)),
        metadatas: Some(meta.into_iter().map(|x| Some(x)).collect()),
        documents: Some(docs.into_iter().map(|x| Some(x)).collect()),
        uris: None,
    };

    collection.add(&payload).await?;

    Ok(())
}

fn get_line(text: &[u8], offset: usize) -> usize {
    text[..=offset].iter().filter(|&&c| c == b'\n').count()
}

fn walk_tree(
    path: &str,
    node: Node,
    text: &[u8],
    meta: &mut Vec<HashMap<String, Value>>,
    docs: &mut Vec<String>,
) {
    let targets: HashSet<&str> = vec![
        "function_definition",
        "struct_specifier",
        "class_specifier",
        "enum_specifier",
    ]
    .into_iter()
    .collect();

    if !targets.contains(node.kind()) {
        let mut cursor = node.walk();
        for child in node.children(&mut cursor) {
            walk_tree(path, child, text, meta, docs);
        }
        return;
    }

    let content = String::from_utf8_lossy(&text[node.byte_range()]);
    docs.push(content.into());

    let mut map: HashMap<String, Value> = HashMap::new();
    map.insert("path".into(), json!(path));
    map.insert(
        "line".into(),
        json!(get_line(text, node.byte_range().start)),
    );
    meta.push(map);
}

async fn index(ollama: &Ollama, collection: &Collection) -> Result<(), Box<dyn std::error::Error>> {
    eprintln!("[*] traversing file system");

    let mut offset: usize = 0;
    let mut meta: Vec<HashMap<String, Value>> = Vec::new();
    let mut docs: Vec<String> = Vec::new();
    for line in stdin().lines() {
        let line: PathBuf = line?.into();
        let lossy_line = line.to_string_lossy();

        let mut file = match File::open(&line).await {
            Ok(file) => file,
            Err(err) => {
                eprintln!("[!] {}: {}", lossy_line, err);
                continue;
            }
        };
        let metadata = match file.metadata().await {
            Ok(metadata) => metadata,
            Err(err) => {
                eprintln!("[!] {}: {}", lossy_line, err);
                continue;
            }
        };

        let Some(ext) = line.extension() else {
            eprintln!("[!] {}: missing extension", lossy_line);
            continue;
        };

        let Some(ext) = ext.to_str() else {
            eprintln!("[!] {} : invalid utf8", lossy_line);
            continue;
        };

        let language = match ext {
            "cpp" | "cxx" | "cc" | "hpp" | "hxx" | "h" => Some(tree_sitter_cpp::LANGUAGE),
            "c" => Some(tree_sitter_c::LANGUAGE),
            _ => {
                eprintln!("[!] *.{} : unknown language", ext);
                None
            }
        };
        
        if let Some(language) = language {

            let mut content: Vec<u8> = Vec::new();
            content.reserve(metadata.len() as usize);

            if let Err(err) = file.read_to_end(&mut content).await {
                eprintln!("[!] {}: {}", lossy_line, err);
                continue;
            }
            
            let mut parser: Parser = Parser::new();
            parser
                .set_language(&language.into())
                .expect("Error loading C++ parser");

            let Some(tree) = parser.parse(&content, None) else {
                eprintln!("[!] {} : could not parse", lossy_line);
                continue;
            };

            walk_tree(
                &lossy_line,
                tree.root_node(),
                &content,
                &mut meta,
                &mut docs,
            );
        } else {
            
            let mut content: String = String::new();
            content.reserve(metadata.len() as usize);
            
            if let Err(err) = file.read_to_string(&mut content).await {
                eprintln!("[!] {}: {}", lossy_line, err);
                continue;
            }
            
            let payload = GenerateEmbeddingsRequest::new(
                EMBEDDING_MODEL.to_string(),
                EmbeddingsInput::Single(content.into())
            );
        }

        if meta.len() >= 512 {
            eprintln!("[*] sending chunk ({})", meta.len());

            add(&ollama, &collection, offset, docs.clone(), meta.clone()).await?;
            offset += meta.len();

            docs.clear();
            meta.clear();
        }
    }

    if meta.len() > 0 {
        eprintln!("[*] sending chunk ({})", meta.len());
        add(&ollama, &collection, offset, docs.clone(), meta.clone()).await?;
    }

    eprintln!("[*] done");

    Ok(())
}

async fn query(ollama: &Ollama, collection: &Collection) -> Result<(), Box<dyn std::error::Error>> {
    let mut input: String = String::new();
    stdin().read_to_string(&mut input)?;

    eprintln!("[*] generating embedding");
    let payload =
        GenerateEmbeddingsRequest::new(EMBEDDING_MODEL.into(), EmbeddingsInput::Single(input));
    let embeddings = ollama.generate_embeddings(payload).await?.embeddings;

    eprintln!("[*] requesting query");
    let payload = QueryRequestPayload {
        where_fields: Default::default(),
        query_embeddings: embeddings,
        ids: None,
        include: Some(vec![Include::Documents, Include::Metadatas]),
        n_results: Some(5),
    };
    let resp = collection.query(&payload, Some(5), None).await?;

    let metadatas = resp.metadatas.unwrap();
    let documents = resp.documents.unwrap();

    let mut chunks: Vec<Code> = Vec::new();
    for (meta, doc) in metadatas
        .last()
        .unwrap()
        .into_iter()
        .zip(documents.last().unwrap())
    {
        let meta = meta.as_ref().unwrap();
        let doc = doc.as_ref().unwrap();

        chunks.push(Code {
            path: meta.get("path").unwrap().to_string(),
            line: meta.get("line").unwrap().as_u64().unwrap(),
            code: doc.to_string(),
        })
    }

    println!("{}", serde_json::to_string_pretty(&chunks)?);

    Ok(())
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    eprintln!("[*] connecting to services");
    let ollama = Ollama::default();
    let khroma = Khroma::new(CHROMA_URL, None)?;

    eprintln!("[*] pulling ollama model");
    ollama
        .pull_model(EMBEDDING_MODEL.to_string(), false)
        .await?;

    eprintln!("[*] initializing database");
    let ef = EmbeddingFunctionConfiguration::Known {
        r#type: "known".into(),
        config: EmbeddingFunctionNewConfiguration {
            name: "ollama".to_string(),
            config: json!({
                "url": OLLAMA_URL,
                "model_name": EMBEDDING_MODEL,
                "timeout": 60
            }),
        },
    };

    let payload = CreateCollectionPayload {
        name: DATABASE_NAME.into(),
        configuration: Some(CollectionConfiguration {
            embedding_function: Some(ef),
            ..Default::default()
        }),
        ..Default::default()
    };

    let tenant = match khroma.create_tenant(DATABASE_NAME).await {
        Ok(tenant) => tenant,
        Err(KhromaError::Api {
            status: StatusCode::CONFLICT,
            ..
        }) => khroma.get_tenant(DATABASE_NAME).await?,
        Err(err) => return Err(err.into()),
    };

    let database = match tenant.create_database(DATABASE_NAME).await {
        Ok(database) => database,
        Err(KhromaError::Api {
            status: StatusCode::CONFLICT,
            ..
        }) => tenant.get_database(DATABASE_NAME).await?,
        Err(err) => return Err(err.into()),
    };

    let collection = database.get_or_create_collection(payload).await?;

    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        return Err("insufficient arguments".into());
    }

    match args[1].as_str() {
        "-i" | "--index" => index(&ollama, &collection).await?,
        "-q" | "--query" => query(&ollama, &collection).await?,
        _ => {
            eprintln!("[!] {} : unknown argument", args[1]);
        }
    }

    Ok(())
}
