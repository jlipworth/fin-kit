# Module Overview

## Dependency Graph

```mermaid
graph TD
    subgraph "Applications"
        A[apps/bond_basis]
    end

    subgraph "Libraries"
        V[viz]
        B[backtest]
        AN[analysis]
        D[data]
        C[core]
    end

    subgraph "External Dependencies"
        QL[QuantLib]
        DDB[DuckDB]
        SPD[spdlog]
        FMT[fmt]
        JSON[nlohmann_json]
    end

    A --> AN
    A --> D

    V --> B
    B --> AN
    AN --> D
    AN --> QL
    D --> C
    D --> DDB

    C --> SPD
    C --> FMT
    C --> JSON
```

## Module Responsibilities

```mermaid
graph LR
    subgraph core["core"]
        C1[Error Handling]
        C2[Financial Types]
        C3[Configuration]
        C4[Logging]
    end

    subgraph data["data"]
        D1[DuckDB Wrapper]
        D2[Parquet Reader]
        D3[Data Schemas]
    end

    subgraph analysis["analysis"]
        A1[Bond Basis]
        A2[CTD Analysis]
        A3[Yield Curves]
    end

    subgraph backtest["backtest"]
        B1[Engine]
        B2[Strategies]
        B3[Execution]
    end

    subgraph viz["viz"]
        V1[Terminal Charts]
        V2[Reports]
    end
```

## Data Flow

```mermaid
flowchart LR
    subgraph Input
        P[Parquet Files]
        DB[(DuckDB)]
    end

    subgraph Processing
        D[data module]
        A[analysis module]
    end

    subgraph Output
        JSON[JSON]
        CSV[CSV]
        TERM[Terminal]
    end

    P --> D
    DB --> D
    D --> A
    A --> JSON
    A --> CSV
    A --> TERM
```

## Bond Basis Analysis Flow

```mermaid
sequenceDiagram
    participant CLI as bond_basis CLI
    participant Data as data module
    participant Analysis as analysis module
    participant QL as QuantLib

    CLI->>Data: Load bond data
    CLI->>Data: Load futures data
    Data-->>CLI: Bond/Futures records

    CLI->>Analysis: Calculate basis
    Analysis->>QL: Price bonds
    Analysis->>QL: Calculate CTD
    QL-->>Analysis: Pricing results
    Analysis-->>CLI: Basis analysis

    CLI->>CLI: Output JSON/CSV
```
