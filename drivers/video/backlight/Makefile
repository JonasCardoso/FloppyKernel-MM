# Backlight & LCD drivers
CFLAGS_lm3530_bl.o += -Os
obj-$(CONFIG_LCD_CLASS_DEVICE)     += lcd.o
obj-$(CONFIG_LCD_CORGI)		   += corgi_lcd.o
obj-$(CONFIG_LCD_HP700)		   += jornada720_lcd.o
obj-$(CONFIG_LCD_L4F00242T03)	   += l4f00242t03.o
obj-$(CONFIG_LCD_LMS283GF05)	   += lms283gf05.o
obj-$(CONFIG_LCD_LTV350QV)	   += ltv350qv.o
obj-$(CONFIG_LCD_ILI9320)	   += ili9320.o
obj-$(CONFIG_LCD_PLATFORM)	   += platform_lcd.o
obj-$(CONFIG_LCD_VGG2432A4)	   += vgg2432a4.o
obj-$(CONFIG_LCD_TDO24M)	   += tdo24m.o
obj-$(CONFIG_LCD_TOSA)		   += tosa_lcd.o
obj-$(CONFIG_LCD_S6E63M0)	+= s6e63m0.o
obj-$(CONFIG_LCD_LD9040)	+= ld9040.o
obj-$(CONFIG_LCD_AMS369FG06)	+= ams369fg06.o

obj-$(CONFIG_BACKLIGHT_CLASS_DEVICE) += backlight.o
obj-$(CONFIG_BACKLIGHT_ATMEL_PWM)    += atmel-pwm-bl.o
obj-$(CONFIG_BACKLIGHT_EP93XX)	+= ep93xx_bl.o
obj-$(CONFIG_BACKLIGHT_GENERIC)	+= generic_bl.o
obj-$(CONFIG_BACKLIGHT_HP700)	+= jornada720_bl.o
obj-$(CONFIG_BACKLIGHT_HP680)	+= hp680_bl.o
obj-$(CONFIG_BACKLIGHT_LOCOMO)	+= locomolcd.o
obj-$(CONFIG_BACKLIGHT_LP855X)	+= lp855x_bl.o
obj-$(CONFIG_BACKLIGHT_OMAP1)	+= omap1_bl.o
obj-$(CONFIG_BACKLIGHT_PANDORA)	+= pandora_bl.o
obj-$(CONFIG_BACKLIGHT_PROGEAR) += progear_bl.o
obj-$(CONFIG_BACKLIGHT_CARILLO_RANCH) += cr_bllcd.o
obj-$(CONFIG_BACKLIGHT_PWM)	+= pwm_bl.o
obj-$(CONFIG_BACKLIGHT_DA903X)	+= da903x_bl.o
obj-$(CONFIG_BACKLIGHT_DA9052)	+= da9052_bl.o
obj-$(CONFIG_BACKLIGHT_MAX8925)	+= max8925_bl.o
obj-$(CONFIG_BACKLIGHT_APPLE)	+= apple_bl.o
obj-$(CONFIG_BACKLIGHT_TOSA)	+= tosa_bl.o
obj-$(CONFIG_BACKLIGHT_SAHARA)	+= kb3886_bl.o
obj-$(CONFIG_BACKLIGHT_WM831X)	+= wm831x_bl.o
obj-$(CONFIG_BACKLIGHT_ADP5520)	+= adp5520_bl.o
obj-$(CONFIG_BACKLIGHT_ADP8860)	+= adp8860_bl.o
obj-$(CONFIG_BACKLIGHT_ADP8870)	+= adp8870_bl.o
obj-$(CONFIG_BACKLIGHT_88PM860X) += 88pm860x_bl.o
obj-$(CONFIG_BACKLIGHT_PCF50633)	+= pcf50633-backlight.o
obj-$(CONFIG_BACKLIGHT_AAT2870) += aat2870_bl.o
obj-$(CONFIG_BACKLIGHT_OT200) += ot200_bl.o

#[V1][sangnae.yoon][2013.02.15] change backlight power 
obj-$(CONFIG_BACKLIGHT_LGE_RT8966) += lge_rt8966_bl.o
#[V1][sangnae.yoon] END

#LGE_CHANGE_S : seven.kim@lge.com kernel3.0 porting
# LGE_CHANGE_S jungrock.oh@lge.com 2013-02-06 add for lm3530_bl_u0
ifeq ($(CONFIG_MACH_MSM7X27A_U0),y)
obj-$(CONFIG_BACKLIGHT_LM3530) += lm3530_bl_u0.o
else
obj-$(CONFIG_BACKLIGHT_LM3530) += lm3530_bl.o
endif
# LGE_CHANGE_E jungrock.oh@lge.com 2013-02-06 add for lm3530_bl_u0
obj-$(CONFIG_BACKLIGHT_LM3528) += lm3528_bl.o
obj-$(CONFIG_BACKLIGHT_BU61800) += bu61800_bl.o
#[V3][junghoon79.kim][2012.9.9] change backlight charge pump
obj-$(CONFIG_BACKLIGHT_RT9396) += rt9396_bl.o
#[V3][junghoon79.kim] END
#LGE_CHANGE_E : seven.kim@lge.com kernel3.0 porting
