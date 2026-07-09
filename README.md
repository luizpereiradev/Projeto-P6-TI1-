# SMN-Q — Sistema de Monitoramento e Notificação de Eventos de Queda

Detector de quedas de baixo custo para pacientes idosos, construído sobre um microcontrolador **ESP32-C3** e um acelerômetro **MPU6050**. Quando uma queda é detectada, o dispositivo alerta localmente (LED, buzzer e display) e notifica o cuidador pelo celular via **Bluetooth Low Energy**.

Trabalho desenvolvido para a disciplina de Trabalho Interdisciplinar I do curso de Sistemas de Informação da **PUC Minas — campus Contagem**.

## Motivação

Quedas são a segunda maior causa de mortes acidentais no mundo entre idosos. O que mais agrava o quadro não é a queda em si, mas o tempo que a vítima permanece caída sem socorro — fenômeno conhecido na literatura como *long lie*. As soluções comerciais disponíveis no Brasil custam entre R$ 150 e R$ 300 por mês, valor inviável para boa parte da população idosa.

O SMN-Q propõe uma alternativa de baixo custo, montada com componentes eletrônicos comuns e sem mensalidade.

## Como funciona

A detecção é feita em **duas fases**, a partir da magnitude resultante da aceleração (`√(x² + y² + z²)`, em g):

1. **Queda livre** — a magnitude cai abaixo de um limiar (o corpo em queda tende a 0 g). Usa-se aqui um filtro de média móvel de 3 amostras, porque essa fase se estende por várias leituras e o sinal cru é ruidoso.
2. **Impacto** — logo depois, a magnitude dispara acima de um segundo limiar. Essa fase usa o valor **cru**, sem filtro: o pico da colisão dura uma ou duas amostras e a média móvel o achataria.

Se o impacto não ocorrer dentro de 1,2 s após a queda livre, o evento é descartado como falso alarme. Exigir a sequência completa (e não apenas um pico de aceleração) evita que atividades comuns — sentar bruscamente, bater o braço na mesa — disparem o alerta.

Confirmada a queda, o sistema entra em uma janela de **15 segundos** em que o usuário pode cancelar o alarme pelo botão. Passado esse tempo sem cancelamento, a emergência é enviada ao cuidador. O mesmo botão funciona como **botão de pânico** a qualquer momento.

### Modos de operação

Um potenciômetro seleciona o perfil de sensibilidade:

| Modo | Limiar de queda livre | Limiar de impacto | Uso |
| --- | --- | --- | --- |
| `REAL` | 0,60 g | 2,50 g | Valores da literatura, para uso efetivo |
| `DEMO` | 0,70 g | 1,15 g | Sensível, para demonstrar sem derrubar o protótipo |

### Sinalização

O LED RGB indica o estado: **verde** monitorando, **azul** queda livre detectada (aguardando impacto), **amarelo** possível queda em janela de cancelamento, **vermelho** emergência enviada.

## Hardware

- ESP32-C3 Super Mini
- Acelerômetro MPU6050 (I2C, endereço `0x68`)
- Display LCD 16x2 com módulo I2C (endereço `0x27`)
- LED RGB (cátodo comum) + resistores
- Buzzer passivo
- Botão de pressão
- Potenciômetro

O MPU6050 e o LCD compartilham o mesmo barramento I2C, o que reduz a fiação.

### Pinagem

| Componente | Pino do componente | ESP32-C3 |
| --- | --- | --- |
| MPU6050 | VCC / GND | 3,3 V / GND |
| MPU6050 | SDA / SCL | GPIO8 / GPIO9 |
| LCD I2C | VCC / GND | 3,3 V / GND |
| LCD I2C | SDA / SCL | GPIO8 / GPIO9 (compartilhados) |
| Potenciômetro | Terminal central | GPIO4 |
| Potenciômetro | Terminais laterais | 3,3 V e GND |
| LED RGB | R / G / B (via resistor) | GPIO3 / GPIO5 / GPIO6 |
| LED RGB | Comum | GND |
| Buzzer | + / − | GPIO10 / GND |
| Botão | Terminais | GPIO2 e GND |

A validação foi feita com alimentação por cabo USB. Para uso portátil, o projeto prevê bateria de íon-lítio 18650 com módulo de carga TP4056 e elevador de tensão MT3608.

## Compilando e gravando

Requer a [Arduino IDE](https://www.arduino.cc/en/software) com o core do ESP32 instalado, mais as bibliotecas:

- `MPU6050_light` (rfetick)
- `LiquidCrystal_I2C`

Selecione a placa **ESP32C3 Dev Module** e grave o `p6ti1.ino`.

## Usando

Abra o Monitor Serial em **115200 baud**. Dois comandos estão disponíveis para teste:

- `q` — simula uma queda, disparando a janela de cancelamento
- `t` — executa a rotina de teste dos componentes (LED, buzzer, sensor, potenciômetro, botão)

Para receber as notificações, procure o dispositivo **`SMN-Q Detector`** em um app cliente BLE (o [nRF Connect](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-mobile) funciona bem) e ative as notificações na característica UART. As mensagens de alerta chegam por ali.

## Estrutura do repositório

```
├── p6ti1.ino          # firmware do ESP32-C3
├── RelatorioTI1P6/    # relatório (versão atual)
│   ├── principal.tex  # preâmbulo, capa, resumo/abstract
│   ├── textos.tex     # corpo do relatório
│   ├── bibliografia.bib
│   ├── figuras/
│   └── principal.pdf  # PDF compilado
└── Relatorio TI1/     # versão anterior do relatório
```

O relatório usa o template ABNTeX2 da PUC Minas (pacote `abakos`). Para compilar:

```sh
cd RelatorioTI1P6
pdflatex principal && bibtex principal && pdflatex principal && pdflatex principal
```

## Autores

- Eduardo Aguiar da Silva
- Lucas Bernardo Souza de Oliveira
- Luiz Felipe da Silva Pereira Santos

Pontifícia Universidade Católica de Minas Gerais — Instituto de Ciências Exatas e de Informática.
