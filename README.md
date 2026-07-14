[English](https://github.com/JamilHsu/ProjectDivaControllerServer/blob/master/README.en-US.md) | 中文

此程式可以把安卓平板或手機變成Hatsune Miku: Project Diva的控制器，類似Switch版的觸控遊玩功能。  
此程式需要與運行在安卓裝置上的[ProjectDivaControllerClient](https://github.com/JamilHsu/ProjectDivaControllerClient)一起搭配使用。  
對於iOS用戶，請使用這個->[ProjectDivaController](https://github.com/JamilHsu/ProjectDivaController)

![image](https://raw.githubusercontent.com/JamilHsu/ProjectDivaControllerServer/refs/heads/master/ProjectDivaController%E9%81%8B%E4%BD%9C%E7%95%AB%E9%9D%A2.jpg)

啟動後，會自動枚舉電腦上的IP位址，將其填入平板/手機上即可連線。  
在初次啟動時防毒軟體可能會來亂(我的這個程式能夠從網路接收攻擊者的命令並用SendInput操作你的鍵盤，桀桀桀桀)，而防火牆則會詢問是否允許存取網路。

連線的方式不拘。USB網路共享、手機行動無線基地台、電腦行動熱點、各自連線至網際網路、`adb reverse tcp:3939 tcp:3939`等都可以成功連線。根據你採用的連線方式，請選擇相應的IP位址。  
請注意，可能有多個IP位置都可以成功連線，但依據所選的IP位址，經過的路徑可能會不同。小心不要明明都接好了USB線結果實際上還是透過無線連接。  
如果你的連線是透過無線的方式連線的，那你可能必須做好被雷格大神[^Lag]針對的心理準備。在我的實測中，延遲最短的方式是透過USB網路共享，以些微的幅度勝過了`adb reverse`。

能成功連線後，應該就沒問題了。如果真沒反應，就在ProjectDivaControllerSettings.txt中將output_received_message和output_keyboard_operation設為1以取得偵錯輸出。
理論上，這樣應該就可以用了，不過還差最後一個步驟:調整按鍵配置。(如果遊戲使用預設配置的話就不需要改)
打開ProjectDivaControllerSettings.txt，編輯各按鍵(△□×◯)對應到的電腦按鍵(WSAD)，或是調整MM+中的鍵盤配置。存檔後重新啟動程式。

隨附的dll和config.toml是一個微型輔助mod，此mod唯一的作用是自動啟動exe並使其在遊戲結束後終止。可以讓這個程式像mod一樣由mod管理器管理。此輔助mod也能使這個程式在Linux上無需特別設定即可運作。如果沒在使用mod的話，可以刪除dll和config.toml。

##### 開發者的閒聊:
從街機轉到主機後用手把玩總覺得不太順手，於是我在Switch上玩的時候總是用觸控遊玩。
後來在電腦上玩時，用鍵盤感覺更不順手了，而小街機也好貴，明明看起來結構也沒有很複雜啊! 不過就只是四個按鍵而已...ㄡ對，還有一個滑條感應器可能會複雜一點。(這時曾萌生了自製小街機控制器的念頭，但並沒有具體付諸行動的想法)

後來我想到，我們平常使用的手機/平板的螢幕，本身不就是一個功能超級加強版感應器嗎?接下來我又想到，與其使用平板作為滑條感應器，我可以直接拿平板用與Switch觸控遊玩相同的方式來玩呀！連組一個不含滑條感應器的小街機的功夫都省了。

於是接下來剩下的唯一一個問題就是，我完全沒有安卓程式開發的經驗。
不過幸好，現在的AI相當強大，雖然可能可靠性還沒有那麼好，但至少比我自己抓瞎好。

~~總之，在與ChatGPT數天的交流下，我成功的能將發生在平板上的觸控操作傳遞給電腦，然後於電腦端分析這些觸控資訊，並轉換為對應的鍵盤輸入。(換句話說，平板上的按鍵畫面完全是裝飾)~~  
~~之所以是在電腦端而不是安卓端轉換觸控輸入，自然是因為我熟悉C++但不熟Kotlin，這樣我才能親自寫好轉換演算法。~~

寫完這套程式後才想到在做之前應該要先上網查查看有沒有人已經做過同樣的東西。不知為何，網路上似乎沒有其他人做過類似的東西，只有看到一個用平板作為滑條的程式，搭配自組的小街機使用。  

2.0版:  
在一段時間後(都過八個月了，因為已經有運作良好的版本所以花了不少時間在打Diva上了哈哈哈)，在這段時間內我除了完成了網頁版的以適用於iPad，還完成了適用於[電腦觸控螢幕的版本](https://github.com/JamilHsu/DivaTapPlay-2.0)(可用於觸控筆電、掌機等)。  
總算放暑假了，我決定來重構這個程式。傳遞觸控操作什麼的根本沒必要，傳遞最終操作結果就好了。而且在安卓端處理移動不但無須傳送一堆移動操作，而且判斷起來也比較準確。還可以讓畫面不要只是靜態的，而是能反映目前的按下狀態。


[^Lag]: 雷格(Lag)大神；典出蝴蝶Seba的《夢天傳說：無盡的旅程》第十章  
https://seba.tw/endless-journey-29/  
https://cxc.today/zh/@seba/work/17373/reader/113814  
(偷偷安利我最喜歡的小說家的小說)