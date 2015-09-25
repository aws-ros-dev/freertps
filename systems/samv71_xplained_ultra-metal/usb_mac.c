#include "metal/usb.h"
//#include "samv71q21.h"
#include <stdio.h>
#include "metal/delay.h"
#include <stdbool.h>

//static bool g_usb_waiting_for_addr_in_pkt;

void usb_init()
{
  printf("usb_init()\r\n");
  PMC->PMC_PCER1 |= 1 << (ID_USBHS - 32);
  USBHS->USBHS_CTRL = 
    USBHS_CTRL_UIMOD | // device mode
    USBHS_CTRL_USBE  ; // enable USB
  PMC->CKGR_UCKR =
    CKGR_UCKR_UPLLEN        | // enable usb pll
    CKGR_UCKR_UPLLCOUNT(0xf); // set lock time
  while(!(PMC->PMC_SR & PMC_SR_LOCKU)); // wait for usb pll lock
  PMC->PMC_USB = 
    PMC_USB_USBS      | // select UPLL for input for FS USB clock (48 MHz)
    PMC_USB_USBDIV(9) ; // FSUSB clock is 480 Mhz / (9 + 1) = 48 MHz
  USBHS->USBHS_DEVIER = USBHS_DEVIER_EORSTES; // enable end-of-reset interrupt
  NVIC_SetPriority(USBHS_IRQn, 1);
  NVIC_EnableIRQ(USBHS_IRQn);
  //USBHS->USBHS_DEVCTRL |= USBHS_DEVCTRL_DETACH; // disable usb output pads
  //delay_ms(10);
  //USBHS->USBHS_DEVCTRL &= ~USBHS_DEVCTRL_DETACH; // enable usb output pads
}

static void usb_reset_ep0()
{
  USBHS->USBHS_DEVEPT |=  USBHS_DEVEPT_EPRST0;
  USBHS->USBHS_DEVEPT &= ~USBHS_DEVEPT_EPRST0; // bring EP0 out of reset
  USBHS->USBHS_DEVIER = USBHS_DEVIER_PEP_0; // enable interrupt for EP0
  USBHS->USBHS_DEVEPTIER[0] = USBHS_DEVEPTIER_RXSTPES;
  USBHS->USBHS_DEVEPT |= USBHS_DEVEPT_EPEN0; // enable endpoint
  USBHS->USBHS_DEVEPTCFG[0] = USBHS_DEVEPTCFG_EPSIZE(3); // EP0 is 64-byte
  USBHS->USBHS_DEVEPTCFG[0] |= USBHS_DEVEPTCFG_ALLOC; // alloc some dpram
  //printf("ep0 status: 0x%08x\r\n", (unsigned)USBHS->USBHS_DEVEPTISR[0]);
}

void usbhs_vector()
{
  //printf("usbhs_vector()\r\n");
  if (USBHS->USBHS_DEVISR & USBHS_DEVISR_EORST)
  {
    USBHS->USBHS_DEVICR = USBHS_DEVICR_EORSTC; // clear end-of-reset bit
    usb_reset_ep0();
    //printf("usb general status: 0x%08x\r\n", (unsigned)USBHS->USBHS_SR);
  }
  else if (USBHS->USBHS_DEVISR & USBHS_DEVISR_PEP_0)
  {
    //printf("ep0 irq\r\n");
    if (USBHS->USBHS_DEVEPTISR[0] & USBHS_DEVEPTISR_RXSTPI)
    {
      uint32_t setup_pkt[2];
      volatile uint32_t *ep0_fifo = (volatile uint32_t *)USBHS_RAM_ADDR;
      setup_pkt[0] = *ep0_fifo;
      __DSB(); // memory sync
      setup_pkt[1] = *ep0_fifo;
      __DSB();
      USBHS->USBHS_DEVEPTICR[0] = USBHS_DEVEPTICR_RXSTPIC; // clear irq flag
      usb_rx_setup((uint8_t *)&setup_pkt[0], 8); // always 8 bytes?
    }
    else
    {
      printf("unknown ep0 irq. ep0 status = 0x%08x\r\n",
          (unsigned)USBHS->USBHS_DEVEPTISR[0]);
      while(1); // trap!
    }
  }
  else
  {
    printf("unhandled usbhs vector: 0x%08x\r\n",
        (unsigned)USBHS->USBHS_DEVISR);
    while (1); // it's a trap
  }
}

void usb_tx(const unsigned ep, const void *buf, const unsigned len)
{
  //printf("usb tx ep%d %d bytes\r\n", (int)ep, (int)len);
  uint8_t *ep_fifo = (uint8_t *)(((volatile uint32_t *)USBHS_RAM_ADDR) 
      + 16384 * ep); // ugly
  const uint8_t *pkt_tx_ptr = buf;
  for (int i = 0; i < len; i++)
    *ep_fifo = *pkt_tx_ptr++;
  //USBHS->USBHS_DEVEPTIFR[0] = USBHS_DEVEPTIFR_TXINIS; // send pkt plz
  USBHS->USBHS_DEVEPTICR[0] = USBHS_DEVEPTICR_TXINIC; // send pkt plz
}

void usb_set_addr(const uint8_t addr)
{
  printf("usb set addr 0x%02x\r\n", (unsigned)addr);
  USBHS->USBHS_DEVCTRL = addr & 0x7f; // ensure it's only a 7-bit addr
  //g_usb_waiting_for_addr_in_pkt = true;
  USBHS->USBHS_DEVEPTICR[0] = USBHS_DEVEPTICR_TXINIC; // send ZLP pkt
  //delay_ms(2); // hack
  USBHS->USBHS_DEVCTRL |= 0x80;
}
