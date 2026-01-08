# Glossary

Financial terms and abbreviations used throughout the fin-kit codebase.

---

## A

**Accrued Interest**
Interest that has accumulated on a bond since the last coupon payment. Added to the clean price to get the dirty (full) price.

**ACT/360**
Day count convention where interest accrues based on actual days elapsed, divided by a 360-day year. Used for USD money markets and most OIS rates.

**ACT/365**
Day count convention using actual days divided by 365. Common for GBP, AUD, and other Commonwealth currencies.

**ADF (Augmented Dickey-Fuller)**
Statistical test for stationarity in time series data. A negative ADF statistic below critical values suggests the series is stationary.

**AONIA**
Australian overnight index. The RBA interbank overnight cash rate, used for AUD OIS curves.

## B

**Basis (Bond)**
The difference between a cash bond price and the equivalent futures price. See gross basis and net basis.

**Basis Point (bp)**
One hundredth of one percent (0.01%). Used for quoting interest rates and spreads. 100 bps = 1%.

**BNOC (Basis Net of Carry)**
Another term for net basis. Gross basis minus the cost of carry to delivery.

**Bootstrapping**
Iterative process to construct a yield curve from market instruments. Each instrument adds a pillar point, solving for the discount factor that reprices it exactly.

## C

**Carry**
The cost or benefit of holding a position over time. For bonds: financing cost minus coupon income received.

**CIP (Covered Interest Parity)**
The theoretical relationship between spot and forward FX rates and interest rate differentials. CIP basis measures deviations from this parity.

**Clean Price**
The quoted bond price excluding accrued interest. What you see on a trading screen.

**Conversion Factor (CF)**
A multiplier that adjusts bond prices for delivery into futures contracts. CME calculates this assuming a 6% yield.

**Convexity**
The second-order sensitivity of bond price to yield changes. Measures the curvature of the price-yield relationship.

**CORRA**
Canadian Overnight Repo Rate Average. The overnight index for CAD OIS curves.

**CTD (Cheapest to Deliver)**
The bond in a futures deliverable basket with the lowest net basis (highest implied repo rate). The seller of a futures contract will deliver this bond.

## D

**Dirty Price**
The full price of a bond including accrued interest: clean price + accrued interest. What you actually pay.

**Discount Factor**
The present value of receiving 1 unit of currency at a future date. Used to discount cash flows.

**Drawdown**
The peak-to-trough decline in portfolio value. Max drawdown is the largest such decline observed.

**DV01 (Dollar Value of 01)**
The change in bond price for a 1 basis point move in yield. A key interest rate risk measure.

## E

**ESTR (Euro Short-Term Rate)**
The euro overnight index, replacing EONIA. Used for EUR OIS curves.

**EWMA (Exponentially Weighted Moving Average)**
A smoothing technique where recent observations have exponentially higher weight than older ones.

## F

**FOMC (Federal Open Market Committee)**
The Fed committee that sets US monetary policy. Meeting dates create discontinuities in short-term rate curves.

**Forward Points**
The difference between a forward FX rate and the spot rate, quoted in pips.

**Forward Rate**
The interest rate for a future period implied by current rates. Can be extracted from the yield curve.

## G

**GC (General Collateral)**
Repo transactions where any eligible security (e.g., any Treasury) can serve as collateral. GC repo rates are a key funding benchmark.

**Gross Basis**
Cash price minus (futures price times conversion factor). Does not account for carry.

## H

**Haircut**
The discount applied to collateral value in repo transactions. A 2% haircut means collateral worth $100 secures a $98 loan.

**High Water Mark**
The highest portfolio value achieved. Used for calculating drawdown and performance fees.

**Hit Rate**
The percentage of trading signals that correctly predicted direction. A simple measure of signal quality.

## I

**IC (Information Coefficient)**
The correlation between a trading signal and subsequent returns. A key measure of signal quality, typically measured via rank correlation.

**Implied Repo Rate**
The financing rate implied by the relationship between cash and futures prices. Calculated as the return from buying a bond and selling it via futures delivery.

**Invoice Price**
The amount received when delivering a bond into a futures contract: futures price times conversion factor plus accrued at delivery.

## M

**Modified Duration**
The percentage change in bond price for a 1% change in yield. First-order interest rate sensitivity.

## N

**NAV (Net Asset Value)**
The total value of portfolio positions plus cash, typically converted to base currency.

**Net Basis**
Gross basis minus carry. Also called BNOC (Basis Net of Carry). The CTD has the lowest net basis.

**NOWA**
Norwegian Overnight Weighted Average. The overnight index for NOK OIS curves.

## O

**OIS (Overnight Index Swap)**
A swap where one leg pays a fixed rate and the other pays the compounded overnight rate. Used for curve construction.

## P

**Pip**
The smallest price increment in FX. Usually 0.0001, but 0.01 for JPY pairs.

**Profit Factor**
The ratio of gross profits to gross losses. Values above 1 indicate profitable trading.

## R

**Repo (Repurchase Agreement)**
A secured borrowing transaction where securities are sold with an agreement to repurchase. The difference between sale and repurchase prices determines the repo rate.

## S

**SARON**
Swiss Average Rate Overnight. The overnight index for CHF OIS curves.

**Sharpe Ratio**
Risk-adjusted return: (portfolio return - risk-free rate) / portfolio volatility. Higher is better, typically annualized.

**SOFR (Secured Overnight Financing Rate)**
The USD overnight rate based on Treasury repo transactions. Published by the NY Fed, it replaced LIBOR for USD.

**SONIA**
Sterling Overnight Index Average. The overnight index for GBP OIS curves.

**Special (Repo)**
When a specific security is in high demand for borrowing, it trades "on special" with a repo rate below GC. Affects basis calculations.

**Spot Date**
Settlement date for FX transactions, typically T+2 (two business days after trade date).

**SR1/SR3**
SOFR futures contracts. SR1 is 1-month, SR3 is 3-month.

**SWESTR**
Swedish krona Short Term Rate. The overnight index for SEK OIS curves.

## T

**Tenor**
The remaining time to maturity of an instrument (e.g., 1M, 3M, 1Y, 10Y).

**TONAR**
Tokyo Overnight Average Rate. The overnight index for JPY OIS curves.

## X

**XCCY Basis (Cross-Currency Basis)**
The spread added to one leg of a cross-currency swap to equate the two legs. Reflects funding differentials between currencies.

## Y

**Yield Curve**
The relationship between interest rates and maturities. Bootstrapped from market instruments.

**YTM (Yield to Maturity)**
The internal rate of return of a bond assuming it is held to maturity and all payments are reinvested at the same rate.

## Z

**Zero Rate**
The yield on a zero-coupon bond. The fundamental building block of yield curves.

**Z-Score**
A standardized value: (observation - mean) / standard deviation. Measures how many standard deviations from the mean.
